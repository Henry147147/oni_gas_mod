#include "oni_domain.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <xmmintrin.h>

namespace ma::oni {
namespace {
using namespace experimental;
constexpr std::array<const char*,4> ids{"nitrogen","oxygen","helium","water"};
std::mutex state_mutex;
std::mutex frame_mutex;
std::atomic<bool> enabled{};
// Geometry is published before enabled; it remains unchanged until the next
// session. Activation/replacement must occur while the previous session is off.
MAOniDomainConfig geometry{};
std::array<std::atomic<std::uint64_t>,MA_ONI_HOOK_COUNT> suppressed{};
std::uint64_t session_sequence{};
MAOniDomainStatus status=[] { MAOniDomainStatus value{}; value.struct_size=sizeof(value); value.abi_version=MA_ONI_DOMAIN_ABI_VERSION; return value; }();
std::unique_ptr<MacClosedWorld> world;
std::vector<MAOniDomainCell> publication;
std::array<long double,4> initial_masses{};
long double initial_energy{};

// Unity can use FTZ/DAZ. Select the required arithmetic locally and restore
// caller rounding, exception flags and SSE controls before returning to it.
struct ArithmeticScope {
    std::fenv_t saved{};
    unsigned int sse=_mm_getcsr();
    ArithmeticScope() {
        if (std::feholdexcept(&saved) != 0) throw std::runtime_error("Cannot save native floating-point environment");
        if (std::fesetround(FE_TONEAREST) != 0) {
            (void)std::fesetenv(&saved); _mm_setcsr(sse);
            throw std::runtime_error("Cannot select the solver's nearest rounding");
        }
        _mm_setcsr((_mm_getcsr() & ~0xe040U) | 0x1f80U);
    }
    ~ArithmeticScope() { (void)std::fesetenv(&saved); _mm_setcsr(sse); }
};

std::shared_ptr<const FluidThermodynamics> model(const SpeciesRegistry& registry) {
    std::vector<ReferenceMaterial> materials;
    for (const auto* id : ids) {
        const auto& species=registry[registry.index(id)]; ReferenceMaterial material;
        material.id=id;
        material.phase=species.phase == Phase::gas ? ReferencePhase::ideal_gas : ReferencePhase::elastic_liquid;
        material.molar_mass_kg_per_mol=species.molar_mass; material.cv_j_per_kg_k=species.cv();
        material.energy_offset_j_per_kg=species.internal_energy(0);
        material.minimum_temperature_k=1; material.maximum_temperature_k=1e6;
        material.zero_pressure_density_kg_per_m3=species.phase == Phase::gas ? 0 : 1000;
        material.reference_bulk_modulus_pa=species.phase == Phase::gas ? 0 : 2.2e9;
        material.maximum_density_ratio=species.phase == Phase::gas ? 1 : 2;
        material.provenance="Sealed ONI development domain: explicit constant-capacity reference, not a complete game catalogue";
        materials.push_back(std::move(material));
    }
    return std::make_shared<const FluidThermodynamics>(registry,"oni-sealed-domain-v1",std::move(materials));
}
std::array<long double,4> masses(const MacClosedWorld& owner) {
    std::array<long double,4> values{};
    for (const auto& cell : owner.state().cells) for (std::size_t k=0;k<ids.size();++k)
        values[k]+=cell.mixture.masses[owner.registry().index(ids[k])];
    return values;
}
long double energy(const MacClosedWorld& owner) {
    const auto& state=owner.state(); long double value=0;
    for (const auto& cell : state.cells) value+=cell.mixture.energy;
    const auto inertia=mac_face_inertias(owner.registry(),state,owner.thermodynamics());
    const auto jx=mac_field_values(state.momentum_x,state.full_momentum_x);
    const auto jy=mac_field_values(state.momentum_y,state.full_momentum_y);
    for (std::size_t i=0;i<jx.size();++i) value+=MacEnergy::kinetic(inertia.x[i],jx[i]).require_representable();
    for (std::size_t i=0;i<jy.size();++i) value+=MacEnergy::kinetic(inertia.y[i],jy[i]).require_representable();
    return value;
}
std::vector<MAOniDomainCell> project(const MacClosedWorld& owner) {
    const auto& state=owner.state();
    const auto inertia=mac_face_inertias(owner.registry(),state,owner.thermodynamics());
    const auto jx=mac_field_values(state.momentum_x,state.full_momentum_x);
    const auto jy=mac_field_values(state.momentum_y,state.full_momentum_y);
    std::vector<MAOniDomainCell> cells; cells.reserve(owner.state().cells.size());
    for (std::size_t index=0;index<state.cells.size();++index) {
        const auto& source=state.cells[index];
        const auto reading=owner.thermodynamics().evaluate(source.mixture,1);
        MAOniDomainCell cell{};
        for (std::size_t k=0;k<ids.size();++k) cell.masses[k]=source.mixture.masses[owner.registry().index(ids[k])];
        cell.internal_energy=source.mixture.energy; cell.pressure=reading.mechanical_pressure_pa;
        cell.temperature=reading.temperature_k;
        // This domain admits either gas-only cells or a completely filled
        // compressed-liquid control. It does not infer fraction from m/rho0.
        cell.liquid_fraction=geometry.scenario == 1 ? 1 : 0;
        const auto x=index%state.grid.width, y=index/state.grid.width;
        const auto half_mass=.5*source.mixture.mass();
        MacEnergySum kinetic; MacScaledSum px, py;
        for (const auto face : {state.grid.x_face(x,y),state.grid.x_face(x+1,y)}) {
            if (inertia.x[face] == 0) continue;
            kinetic.add(MacEnergy::kinetic(inertia.x[face],jx[face]).shared(half_mass,inertia.x[face]));
            px.add(jx[face].shared(half_mass,inertia.x[face]));
        }
        for (const auto face : {state.grid.y_face(x,y),state.grid.y_face(x,y+1)}) {
            if (inertia.y[face] == 0) continue;
            kinetic.add(MacEnergy::kinetic(inertia.y[face],jy[face]).shared(half_mass,inertia.y[face]));
            py.add(jy[face].shared(half_mass,inertia.y[face]));
        }
        cell.resolved_kinetic_energy=kinetic.value().require_representable();
        cell.momentum_x=px.value().require_representable(); cell.momentum_y=py.value().require_representable();
        cells.push_back(cell);
    }
    return cells;
}
void fault(const char* error) noexcept {
    status.state=3;
    std::snprintf(status.error,sizeof(status.error),"%s",error);
    // Keep the native exclusions active. Failure never re-enables native fluid
    // physics over the compatibility cells; the driver must end the session.
}
}

void start_domain(const MAOniDomainConfig& config,std::uint64_t thread) {
    if (config.struct_size != sizeof(config) || config.abi_version != MA_ONI_DOMAIN_ABI_VERSION ||
        config.flags != MA_ONI_DOMAIN_SEALED_TEST || config.reserved || config.scenario > 1 ||
        config.width < 2 || config.height < 2 || config.width > 16 || config.height > 16 ||
        config.native_grid_width < config.width+2 || config.offset_x == 0 || config.offset_y == 0 ||
        static_cast<std::uint64_t>(config.offset_x)+config.width >= config.native_grid_width ||
        (static_cast<std::uint64_t>(config.offset_y)+config.height+2)*(static_cast<std::uint64_t>(config.native_grid_width)+2) > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid sealed native domain geometry, model or opt-in");
    std::lock_guard frame_lock(frame_mutex);
    std::lock_guard lock(state_mutex);
    if (enabled.load(std::memory_order_acquire)) throw std::logic_error("A sealed native domain is already active");
    const ArithmeticScope arithmetic;
    const auto registry=SpeciesRegistry::standard(); const auto eos=model(registry);
    MacTransportState state;
    state.grid={config.width,config.height,1,1,false,false,std::vector<std::uint8_t>(config.width*config.height)};
    for (std::uint32_t y=0;y<config.height;++y) for (std::uint32_t x=0;x<config.width;++x) {
        std::vector<double> values(registry.size());
        if (config.scenario == 1) values[registry.index("water")]=1100;
        else {
            const double oxygen=.18+.12*static_cast<double>(x)/static_cast<double>(config.width-1);
            const double pressure=100000+1000*static_cast<double>(x)/static_cast<double>(config.width-1);
            const double coefficient=gas_constant*((1-oxygen)/registry[registry.index("nitrogen")].molar_mass+
                oxygen/registry[registry.index("oxygen")].molar_mass+1e-28/registry[registry.index("helium")].molar_mass);
            const double gas=pressure/(coefficient*300);
            values[registry.index("nitrogen")]=(1-oxygen)*gas; values[registry.index("oxygen")]=oxygen*gas;
            values[registry.index("helium")]=1e-28*gas;
        }
        state.cells.push_back({eos->initialize_at_temperature(values,300,1).mixture,{}});
    }
    const auto inertia=mac_face_inertias(registry,state,*eos);
    state.momentum_x.resize(inertia.x.size()); state.momentum_y.resize(inertia.y.size());
    MacClosedSettings settings; settings.maximum_timestep_s=.01;
    auto candidate=std::make_unique<MacClosedWorld>(registry,eos,std::move(state),settings);
    // All allocations and checks happen before publishing the activation flag.
    geometry=config;
    auto cells=project(*candidate);
    const auto totals=masses(*candidate); const auto total_energy=energy(*candidate);
    MAOniDomainStatus next{}; next.struct_size=sizeof(next); next.abi_version=MA_ONI_DOMAIN_ABI_VERSION;
    next.state=1; next.cells=static_cast<std::uint32_t>(cells.size()); next.session=++session_sequence;
    next.start_thread=thread; next.initial_oxygen_kg=cells.front().masses[1]; next.current_oxygen_kg=next.initial_oxygen_kg;
    next.pressure_pa=cells.front().pressure; next.liquid_mass_kg=cells.front().masses[3];
    world=std::move(candidate); publication=std::move(cells); initial_masses=totals; initial_energy=total_energy;
    status=next;
    for (auto& value : suppressed) value=0;
    enabled.store(true,std::memory_order_release);
}
void stop_domain() noexcept {
    // Stopping while the native sim runs would expose scalar projections to
    // original fluid physics. Only shutdown_domain is allowed to release it.
    std::lock_guard lock(state_mutex);
    if (status.state == 1) status.state=2;
}
void shutdown_domain() noexcept {
    std::lock_guard lock(state_mutex);
    enabled.store(false,std::memory_order_release);
    if (status.state == 1) status.state=2;
    world.reset();
}
bool domain_active() noexcept { return enabled.load(std::memory_order_acquire); }
std::mutex& domain_frame_mutex() noexcept { return frame_mutex; }
MAOniDomainSpecies domain_species(std::uint32_t index) {
    if (index > ids.size()) throw std::invalid_argument("Unknown sealed-domain species index");
    const ArithmeticScope arithmetic;
    const auto registry=SpeciesRegistry::standard();
    const auto& species=registry[registry.index(index == ids.size() ? "carbon_dioxide" : ids[index])];
    return {species.molar_mass,species.cv(),species.internal_energy(0),species.phase == Phase::gas ? 0.0 : 1000.0};
}
bool suppress_domain_cell(std::uint32_t hook,int cell) noexcept {
    if (!enabled.load(std::memory_order_acquire) || cell < 0) return false;
    const auto position=static_cast<std::uint32_t>(cell);
    // ONI's public grid omits a one-cell native halo. Every per-cell phase
    // receives an index in the padded grid, not a GameData/public cell index.
    const auto pitch=geometry.native_grid_width+2;
    const auto native_x=position%pitch, native_y=position/pitch;
    if (!native_x || !native_y) return false;
    const auto x=native_x-1, y=native_y-1;
    if (x < geometry.offset_x || x-geometry.offset_x >= geometry.width ||
        y < geometry.offset_y || y-geometry.offset_y >= geometry.height) return false;
    if (hook < suppressed.size()) suppressed[hook].fetch_add(1,std::memory_order_relaxed);
    return true;
}
void advance_domain(std::uint64_t ticket,double seconds,std::uint64_t thread) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    std::lock_guard lock(state_mutex);
    if (status.state != 1) return;
    try {
        const ArithmeticScope arithmetic;
        if (!ticket || ticket <= status.native_ticket || !std::isfinite(seconds) || seconds <= 0 || seconds > 1)
            throw std::runtime_error("Native frame ticket or timestep is invalid");
        if (thread == status.start_thread) throw std::runtime_error("Custom solver did not execute on a native worker");
        auto result=world->advanced(seconds,4096);
        auto cells=project(result.world); auto next=status;
        const auto totals=masses(result.world);
        for (std::size_t i=0;i<totals.size();++i) {
            if (initial_masses[i] == 0) {
                if (totals[i] != 0) throw std::runtime_error("Native domain created an absent constituent");
            } else next.maximum_relative_mass_error=std::max(next.maximum_relative_mass_error,
                static_cast<double>(std::abs((totals[i]-initial_masses[i])/initial_masses[i])));
        }
        next.maximum_relative_energy_error=std::max(next.maximum_relative_energy_error,
            static_cast<double>(std::abs((energy(result.world)-initial_energy)/initial_energy)));
        if (next.maximum_relative_mass_error > 1e-8 || next.maximum_relative_energy_error > 1e-6)
            throw std::runtime_error("Native domain exceeded its mass/energy drift bound");
        next.substeps+=result.report.substeps; next.rejected_attempts+=result.report.rejected_attempts;
        ++next.completed_frames; next.native_ticket=ticket; next.time_seconds=result.world.time_s();
        next.last_timestep_seconds=seconds; next.solver_thread=thread;
        next.current_oxygen_kg=cells.front().masses[1]; next.pressure_pa=cells.front().pressure;
        next.liquid_mass_kg=cells.front().masses[3];
        auto owner=std::make_unique<MacClosedWorld>(std::move(result.world));
        world=std::move(owner); publication=std::move(cells); status=next;
    } catch (const std::exception& error) { fault(error.what()); }
      catch (...) { fault("Unknown custom native worker failure"); }
}
void copy_domain(MAOniDomainStatus& output,MAOniDomainCell* cells,std::uint32_t capacity) {
    std::lock_guard frame_lock(frame_mutex);
    std::lock_guard lock(state_mutex);
    if (cells && capacity < publication.size()) throw std::length_error("Native domain snapshot capacity is insufficient");
    if (!cells && capacity) throw std::invalid_argument("Null native domain cell output");
    auto next=status;
    next.suppressed_pressure=suppressed[MA_ONI_PRESSURE].load(std::memory_order_relaxed);
    next.suppressed_liquid=suppressed[MA_ONI_LIQUID].load(std::memory_order_relaxed)+
        suppressed[MA_ONI_LIQUID_DISPLACEMENT].load(std::memory_order_relaxed);
    next.suppressed_temperature=suppressed[MA_ONI_TEMPERATURE].load(std::memory_order_relaxed);
    next.suppressed_transition=suppressed[MA_ONI_STATE_TRANSITION].load(std::memory_order_relaxed);
    next.suppressed_post_process=suppressed[MA_ONI_POST_PROCESS].load(std::memory_order_relaxed);
    next.suppressed_gas=suppressed[MA_ONI_GAS].load(std::memory_order_relaxed)+suppressed[MA_ONI_GAS_PAIR].load(std::memory_order_relaxed)+
        suppressed[MA_ONI_GAS_DISPLACEMENT].load(std::memory_order_relaxed);
    if (cells) std::copy(publication.begin(),publication.end(),cells);
    output=next;
}
}
