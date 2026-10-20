#include "oni_domain.hpp"
#include "mac_block_execution.hpp"
#include "oni_radial_consumption.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include "mixed_atmospheres/mac_native_heat.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include "oni_actor_checkpoint.hpp"
#include "oni_configured_domain.hpp"
#include "oni_frame_inputs.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <limits>
#include <memory>
#include <map>
#include <set>
#include <bit>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>
#include <xmmintrin.h>

namespace ma::oni {
namespace {
using namespace experimental;
constexpr std::array<const char*,5> ids{"nitrogen","oxygen","helium","water","carbon_dioxide"};
constexpr bool liquid_scenario(std::uint32_t scenario) { return scenario==1 || scenario==3; }
constexpr bool mixed_scenario(std::uint32_t scenario) { return scenario==4 || scenario==5 || scenario==6; }
constexpr bool admitted_species(std::uint32_t scenario,std::uint32_t species) {
    return mixed_scenario(scenario) || liquid_scenario(scenario)==(species==3);
}
constexpr bool gravity_scenario(std::uint32_t scenario) { return scenario==2 || scenario==3; }
// Configured sessions (scenario 7) take physics from an explicit record and
// never from a scenario number. Legacy scenarios keep the helpers above.
constexpr bool configured_scenario(std::uint32_t scenario) { return scenario==MA_ONI_DOMAIN_CONFIGURED_SCENARIO; }
constexpr std::uint32_t configured_physics_flags=MA_ONI_DOMAIN_PHYSICS_GRAVITY|MA_ONI_DOMAIN_PHYSICS_CONDUCTION|
    MA_ONI_DOMAIN_PHYSICS_VISCOSITY|MA_ONI_DOMAIN_PHYSICS_MIXTURE|MA_ONI_DOMAIN_PHYSICS_LIQUID|MA_ONI_DOMAIN_PHYSICS_VACUUM|
    MA_ONI_DOMAIN_PHYSICS_UNRESOLVED_GAS;
bool void_domain(const MAOniDomainPhysics& p) { return (p.flags&MA_ONI_DOMAIN_PHYSICS_VACUUM)!=0; }
bool mixed_domain(const MAOniDomainConfig& g,const MAOniDomainPhysics& p) {
    return configured_scenario(g.scenario) ? (p.flags&MA_ONI_DOMAIN_PHYSICS_MIXTURE)!=0 : mixed_scenario(g.scenario);
}
bool liquid_domain(const MAOniDomainConfig& g,const MAOniDomainPhysics& p) {
    return configured_scenario(g.scenario) ? (p.flags&MA_ONI_DOMAIN_PHYSICS_LIQUID)!=0 : liquid_scenario(g.scenario);
}
bool admitted_domain_species(const MAOniDomainConfig& g,const MAOniDomainPhysics& p,std::uint32_t species) {
    return mixed_domain(g,p) || liquid_domain(g,p)==(species==3);
}
bool positive_zero(double value) { return std::bit_cast<std::uint64_t>(value)==0; }
bool same_bits(double a,double b) { return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b); }
std::mutex state_mutex;
std::mutex frame_mutex;
std::atomic<bool> enabled{};
// Geometry is published before enabled; it remains unchanged until the next
// session. Activation/replacement must occur while the previous session is off.
MAOniDomainConfig geometry{};
// Published with geometry. Legacy sessions keep a zero record and no solids.
// The fixed array never reallocates under a native hook reading it.
MAOniDomainPhysics physics_settings{};
std::array<std::atomic<std::uint8_t>,MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS> solid_cells{};
std::array<std::atomic<std::uint64_t>,MA_ONI_HOOK_COUNT> suppressed{};
std::uint64_t session_sequence{};
MAOniDomainStatus status=[] { MAOniDomainStatus value{}; value.struct_size=sizeof(value); value.abi_version=MA_ONI_DOMAIN_ABI_VERSION; return value; }();
std::shared_ptr<const MacClosedWorld> world;
std::unique_ptr<MacClosedWorld> pending_world;
std::uint64_t accepted_heat_batches{}, committed_heat_batches{};
std::uint32_t pending_heat_batches{}, pending_heat_edits{};
MacEnergySum committed_heat, pending_heat;
std::array<MacScaledSum,MA_ONI_DOMAIN_SPECIES> committed_mass, pending_mass;
MacEnergySum committed_material, pending_material, committed_kinetic, pending_kinetic;
std::uint64_t accepted_transfer_batches{}, committed_transfer_batches{}, receipt_sequence{}, acknowledged_receipt{};
std::uint32_t pending_transfer_batches{};
std::vector<MAOniDomainReceipt> pending_receipts, receipts;
std::vector<frame_inputs::Batch> pending_inputs;
std::vector<std::uint8_t> completed_inputs,completed_snapshot;
struct DeferredFrameAudit {
    std::shared_ptr<const MacClosedWorld> source, completed;
    MAOniDomainPhysics physics{};
    frame_inputs::Image journal;
    std::vector<actor_checkpoint::State> source_actors, completed_actors;
};
std::optional<DeferredFrameAudit> completed_audit;
std::uint64_t audit_full_serializations{},audit_journal_serializations{};
std::uint32_t pending_input_flags{};
struct Actor {
    MAOniDomainActor config{}; double elapsed{}, rate_remainder{}; bool blocked{};
    actor_checkpoint::Identity identity{};
};
std::map<std::pair<std::uint32_t,std::int32_t>,Actor> actors;
using ActorIdentityKey=std::tuple<std::uint32_t,actor_checkpoint::Identity>;
std::map<ActorIdentityKey,actor_checkpoint::State> pending_actor_restore;
std::uint64_t accepted_actor_configurations{}, actor_steps{}, consumer_samples{}, emitter_samples{};
std::uint64_t thermal_stages{},thermal_contacts{},thermal_iterations{},unresolved_thermal_updates{},thermal_equilibrium_intervals{};
MacEnergySum thermal_redistribution,thermal_entropy;
MacDiffusionReport diffusion_progress;
std::uint64_t viscous_stages{},viscous_modes{},unresolved_viscous_heat{},unresolved_viscous_momentum{};
MacEnergySum viscous_dissipation,viscous_stored_heat,viscous_entropy,viscous_wall_x,viscous_wall_y;
std::uint64_t subcell_void_events{};
MacEnergySum subcell_void_entropy;
struct ChunkHeatProgress {
    std::uint64_t contacts{},transfers{},unresolved{},iterations{};
    MacEnergySum body_loss,fluid_heat,entropy,requested;
    double maximum_residual{},maximum_bound{};
    MAOniDomainSolidHeatEndpoint endpoint{};
};
ChunkHeatProgress pending_chunk_heat,committed_chunk_heat,pending_solid_heat,committed_solid_heat;
std::map<int,MAOniDomainSolidHeatCell> solid_heat_cells;
std::set<std::pair<int,int>> solid_heat_contacts;
MAOniDomainSolidCellState completed_solid_cell{};
MacEnergySum committed_potential,pending_potential,body_impulse,gravity_internal_work;
double initial_potential{},maximum_energy_residual{};
std::vector<MAOniDomainCell> publication;
std::array<long double,MA_ONI_DOMAIN_SPECIES> initial_masses{};
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
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        // Unity also selects x87 double precision. GNU long double still
        // advertises a 64-bit significand, so inheriting that control word
        // silently reduces the EOS root's working precision. Select its
        // declared precision locally; fesetenv restores the caller on exit.
        if constexpr (std::numeric_limits<long double>::digits > std::numeric_limits<double>::digits) {
            unsigned short control=0;
            __asm__ volatile("fnstcw %0" : "=m"(control));
            control=static_cast<unsigned short>(control | 0x0300U);
            __asm__ volatile("fldcw %0" : : "m"(control));
        }
#endif
    }
    ~ArithmeticScope() { (void)std::fesetenv(&saved); _mm_setcsr(sse); }
};

constexpr char liquid_gravity_profile[]="oni-sealed-domain-liquid-gravity-v1";
constexpr double liquid_reference_temperature=300;
SpeciesRegistry domain_registry(std::uint32_t scenario) {
    auto registry=SpeciesRegistry::standard();
    if (scenario!=3) return registry;
    SpeciesRegistry shifted;
    for (auto species : registry.all()) {
        // An affine coordinate change, not a change to physical formation
        // energy. The versioned owner stores water's anomaly about 300 K;
        // physical_internal_energy and every boundary account retain the
        // original per-mass reference separately. Small mechanical/thermal
        // updates no longer round against the large chemical reference.
        if (species.id=="water")
            species.formation_enthalpy=-species.cv()*(liquid_reference_temperature-reference_temperature)*species.molar_mass;
        shifted.add(std::move(species));
    }
    return shifted;
}
std::shared_ptr<const FluidThermodynamics> model(const SpeciesRegistry& registry,std::uint32_t scenario=0) {
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
    return std::make_shared<const FluidThermodynamics>(registry,scenario==3 ? liquid_gravity_profile : "oni-sealed-domain-v2",std::move(materials));
}
bool separated_reference_energy(const MacClosedWorld& owner) {
    return owner.thermodynamics().reference().profile_version()==liquid_gravity_profile;
}
MacEnergySum physical_reference_energy(const MacClosedWorld& owner,std::size_t species,double mass) {
    MacEnergySum result;
    if (!separated_reference_energy(owner) || owner.registry()[species].id!="water") return result;
    static const auto physical_registry=SpeciesRegistry::standard();
    MacEnergySum offset;
    offset.add(physical_registry[physical_registry.index("water")].internal_energy(0));
    offset.add(-owner.registry()[species].internal_energy(0));
    for (std::size_t k=0;k<offset.size();++k) result.add(offset.component(k).multiplied(mass));
    return result;
}
MacEnergySum physical_internal_energy(const MacClosedWorld& owner,const MacMaterialCell& cell) {
    MacEnergySum result;account_mac_cell_internal_energy(result,cell);
    const auto& mixture=cell.mixture;
    if (separated_reference_energy(owner)) for (std::size_t k=0;k<mixture.masses.size();++k) {
        const auto reference=physical_reference_energy(owner,k,mixture.masses[k]);
        for (std::size_t n=0;n<reference.size();++n) result.add(reference.component(n));
    }
    return result;
}
std::array<long double,MA_ONI_DOMAIN_SPECIES> masses(const MacClosedWorld& owner) {
    std::array<long double,MA_ONI_DOMAIN_SPECIES> values{};
    for (const auto& cell : owner.state().cells) for (std::size_t k=0;k<ids.size();++k)
        values[k]+=cell.mixture.masses[owner.registry().index(ids[k])];
    return values;
}
double potential(const MacClosedWorld& owner) {
    MacEnergySum total; const auto& state=owner.state();
    for (std::size_t i=0;i<state.cells.size();++i)
        total.add(MacEnergy::from_double(state.cells[i].mixture.mass()).multiplied(owner.settings().gravity_m_per_s2)
            .multiplied((static_cast<double>(i/state.grid.width)+.5)*state.grid.spacing_m));
    return total.value().require_representable();
}
long double energy(const MacClosedWorld& owner) {
    const auto& state=owner.state(); long double value=0;
    for (const auto& cell : state.cells) {
        const auto internal=physical_internal_energy(owner,cell);
        for (std::size_t k=0;k<internal.size();++k) value+=mac_energy_long_double(internal.component(k));
    }
    const auto inertia=mac_face_inertias(owner.registry(),state,owner.thermodynamics());
    const auto jx=mac_field_values(state.momentum_x,state.full_momentum_x);
    const auto jy=mac_field_values(state.momentum_y,state.full_momentum_y);
    for (std::size_t i=0;i<jx.size();++i) value+=MacEnergy::kinetic(inertia.x[i],jx[i]).require_representable();
    for (std::size_t i=0;i<jy.size();++i) value+=MacEnergy::kinetic(inertia.y[i],jy[i]).require_representable();
    return value+potential(owner);
}
std::vector<MAOniDomainCell> project(const MacClosedWorld& owner,const MAOniDomainConfig& g,const MAOniDomainPhysics& p) {
    const auto& state=owner.state();
    const auto inertia=mac_face_inertias(owner.registry(),state,owner.thermodynamics());
    const auto jx=mac_field_values(state.momentum_x,state.full_momentum_x);
    const auto jy=mac_field_values(state.momentum_y,state.full_momentum_y);
    std::vector<MAOniDomainCell> cells; cells.reserve(owner.state().cells.size());
    for (std::size_t index=0;index<state.cells.size();++index) {
        // Solids and exact vacuum publish zero records. The configuration mask
        // distinguishes an owned empty fluid cell from an unowned solid.
        if (state.grid.walls[index]) { cells.push_back(MAOniDomainCell{}); continue; }
        const auto& source=state.cells[index];
        // A vacuum cell (void phase) also publishes an all-zero record.
        if (!(source.mixture.mass()>0)) { cells.push_back(MAOniDomainCell{}); continue; }
        const auto reading=owner.thermodynamics().evaluate(source.mixture,mac_material_volume(state,index));
        MAOniDomainCell cell{};
        for (std::size_t k=0;k<ids.size();++k) cell.masses[k]=source.mixture.masses[owner.registry().index(ids[k])];
        cell.internal_energy=physical_internal_energy(owner,source).value().require_representable(); cell.pressure=reading.mechanical_pressure_pa;
        cell.temperature=reading.temperature_k;
        // Single-phase controls retain their declared filled phase. The
        // explicit mixed scenario publishes the actual elastic liquid volume.
        cell.liquid_fraction=mixed_domain(g,p) || void_domain(p) ? reading.liquid_volume_m3 : liquid_domain(g,p) ? 1 : 0;
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
// Status scalars sample the first fluid cell in row-major order: cell 0 in
// every legacy scenario, which has no solids.
std::size_t reported_cell(const MacClosedWorld& owner) {
    const auto& walls=owner.state().grid.walls;
    const auto found=std::find(walls.begin(),walls.end(),std::uint8_t{0});
    if (found==walls.end()) throw std::invalid_argument("Native domain has no fluid cell");
    return static_cast<std::size_t>(found-walls.begin());
}
// Configured sessions export the checksummed MADCFG01 wrapper; legacy
// sessions keep exactly the solver image they always exported.
std::vector<std::uint8_t> physical_image(const MacClosedWorld& owner,const MAOniDomainConfig& g,const MAOniDomainPhysics& p) {
    if (!configured_scenario(g.scenario)) return owner.checkpoint();
    return configured_domain::encode({g,p,owner.state().grid.walls,owner.checkpoint()});
}
std::vector<std::uint8_t> audit_image(const MacClosedWorld& owner,const MAOniDomainConfig& g,const MAOniDomainPhysics& p,
    const std::vector<actor_checkpoint::State>& saved_actors) {
    auto image=physical_image(owner,g,p);
    if (saved_actors.empty()) return image;
    return actor_checkpoint::encode({g,saved_actors,std::move(image)});
}
MacClosedSettings configured_settings(const MAOniDomainPhysics& physics) {
    // Same construction as the legacy scenarios: disabled values stay +0.
    MacClosedSettings settings; settings.maximum_timestep_s=.01;
    settings.numerical_kernel=MacNumericalKernel::production;
    settings.thermal_conduction=(physics.flags&MA_ONI_DOMAIN_PHYSICS_CONDUCTION)!=0;
    if (physics.flags&MA_ONI_DOMAIN_PHYSICS_VISCOSITY) settings.viscosity={physics.shear_viscosity_pa_s,physics.bulk_viscosity_pa_s};
    settings.gravity_m_per_s2=physics.gravity_m_per_s2;
    // Vacuum: the void phase with sharp interfaces (native/MAC_VACUUM.md).
    if (void_domain(physics)) { settings.vacuum=MacVacuumRepresentation::void_phase; settings.reconstruction=MacMaterialReconstruction::interface_plic; }
    if (physics.flags&MA_ONI_DOMAIN_PHYSICS_UNRESOLVED_GAS) settings.subcell_void=MacSubcellVoidPolicy::expand_unresolved_gas;
    return settings;
}
MacClosedAdvance advance_owned_world(const MacClosedWorld& source,double seconds,
    MacExecutionPolicy policy=MacExecutionPolicy::exact_equilibrium) {
    // Application execution policy only: fixed blocks cap small rooms, and
    // worker count never changes physical results or checkpoint continuation.
    static const bool executor_configured=[] {
        auto options=mac_kernel_default_options();options.threads=12;
        set_mac_kernel_default_options(options);return true;
    }();
    (void)executor_configured;
    return source.advanced(seconds,4096,policy);
}
void fault(const char* error) noexcept {
    status.state=3;
    std::snprintf(status.error,sizeof(status.error),"%s",error);
    // Keep the native exclusions active. Failure never re-enables native fluid
    // physics over the compatibility cells; the driver must end the session.
}
MAOniDomainHeatStatus heat_status() {
    return {sizeof(MAOniDomainHeatStatus),1,status.state,pending_heat_batches,status.session,
        accepted_heat_batches,committed_heat_batches,committed_heat.value().require_representable(),pending_heat.value().require_representable()};
}
MacEnergySum combined_heat(const MacEnergySum& first,const MacEnergySum& second) {
    auto result=first;
    for (std::size_t i=0;i<second.size();++i) result.add(second.component(i));
    return result;
}
ChunkHeatProgress combined_chunk_heat(const ChunkHeatProgress& a,const ChunkHeatProgress& b) {
    ChunkHeatProgress result{a.contacts+b.contacts,a.transfers+b.transfers,a.unresolved+b.unresolved,a.iterations+b.iterations,
        combined_heat(a.body_loss,b.body_loss),combined_heat(a.fluid_heat,b.fluid_heat),combined_heat(a.entropy,b.entropy),combined_heat(a.requested,b.requested),
        std::max(a.maximum_residual,b.maximum_residual),std::max(a.maximum_bound,b.maximum_bound),b.contacts ? b.endpoint : a.endpoint};
    for (const auto* value:{&result.body_loss,&result.fluid_heat,&result.entropy,&result.requested})
        (void)value->value().require_representable();
    return result;
}
// All callers hold the frame/state locks. This work owns every allocation and
// account; neither actor preparation nor a later failed advance can publish it.
struct GasWork {
    std::unique_ptr<MacClosedWorld> owner=std::make_unique<MacClosedWorld>(pending_world ? *pending_world : *world);
    std::array<MacScaledSum,MA_ONI_DOMAIN_SPECIES> mass=pending_mass;
    MacEnergySum material=pending_material,kinetic=pending_kinetic,potential=pending_potential;
    std::vector<MAOniDomainReceipt> outbox=pending_receipts;
    std::uint64_t sequence=receipt_sequence;
    MAOniDomainConfig configuration=geometry;
    MAOniDomainPhysics physics=physics_settings;
    std::uint64_t session=status.session,frame=status.completed_frames;
    bool live=true;
    GasWork()=default;
    GasWork(const MacClosedWorld& source,const MAOniDomainConfig& g,const MAOniDomainPhysics& p)
        : owner(std::make_unique<MacClosedWorld>(source)),mass{},material{},kinetic{},potential{},outbox{},sequence{},
          configuration(g),physics(p),session(1),frame(0),live(false) {}
    void append(MAOniDomainReceipt receipt) {
        if ((live ? receipts.size() : 0)+outbox.size() >= 4096) throw std::length_error("Native actor/transfer receipt capacity exhausted");
        if (sequence == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native receipt sequence overflow");
        receipt.sequence=++sequence; receipt.session=session; outbox.push_back(receipt);
    }
    void validate_accounts() const {
        (void)material.value().require_representable(); (void)kinetic.value().require_representable(); (void)potential.value().require_representable();
        if (!live) { for (const auto& value : mass) (void)value.value().require_representable(); return; }
        (void)combined_heat(committed_material,material).value().require_representable();
        (void)combined_heat(committed_kinetic,kinetic).value().require_representable();
        (void)combined_heat(committed_potential,potential).value().require_representable();
        for (std::size_t i=0;i<mass.size();++i) {
            (void)mass[i].value().require_representable();
            (void)combined_heat(committed_mass[i],mass[i]).value().require_representable();
        }
    }
};
std::size_t domain_index(std::uint32_t public_cell,const MAOniDomainConfig& g) {
    const auto x=public_cell%g.native_grid_width,y=public_cell/g.native_grid_width;
    if (x < g.offset_x || x-g.offset_x >= g.width || y < g.offset_y || y-g.offset_y >= g.height)
        throw std::invalid_argument("Native transfer/actor target is outside the domain");
    return x-g.offset_x+(y-g.offset_y)*g.width;
}
std::size_t domain_index(std::uint32_t public_cell) { return domain_index(public_cell,geometry); }
std::unique_ptr<MacClosedWorld> prepare_heat(const MacClosedWorld& source,const MAOniDomainConfig& g,
    std::uint64_t session,std::uint64_t frame,std::span<const MAOniDomainHeat> inputs,MacEnergySum& delta) {
    std::vector<MacCellHeat> edits;edits.reserve(inputs.size());
    for (const auto& input:inputs) {
        if (input.struct_size!=sizeof(input) || input.abi_version!=1)
            throw std::invalid_argument("Native heat input ABI differs");
        if (input.session!=session || input.expected_frame!=frame)
            throw std::logic_error("Native heat source session/frame is stale");
        const auto cell=domain_index(input.public_cell,g);
        if (source.state().grid.walls[cell]) throw std::invalid_argument("Native heat targets a configured solid cell");
        edits.push_back({cell,input.joules,input.maximum_temperature_k});delta.add(input.joules);
    }
    return std::make_unique<MacClosedWorld>(source.with_cell_heat(edits));
}
MAOniDomainReceipt prepare_point_transfer(GasWork& work,const MAOniDomainTransfer& input,std::uint32_t receipt_kind) {
    if (input.struct_size != sizeof(input) || input.abi_version != MA_ONI_DOMAIN_TRANSFER_ABI_VERSION || input.reserved || input.flags > 1 ||
        (input.kind != 1 && input.kind != 2) || input.species >= ids.size() ||
        !admitted_domain_species(work.configuration,work.physics,input.species) ||
        !std::isfinite(input.mass_kg) || input.mass_kg < 0 || !std::isfinite(input.temperature_k))
        throw std::invalid_argument("Native fluid transfer input differs from its ABI/model");
    if (input.session != work.session || input.expected_frame != work.frame)
        throw std::logic_error("Native fluid transfer source session/frame is stale");
    const auto cell=domain_index(input.public_cell,work.configuration),species=work.owner->registry().index(ids[input.species]);
    if (work.owner->state().grid.walls[cell]) throw std::invalid_argument("Native fluid transfer targets a configured solid cell");
    double amount=input.mass_kg;
    if (input.kind == 2) amount=std::min(amount,work.owner->state().cells[cell].mixture.masses[species]);
    if (input.flags) {
        float projected=static_cast<float>(amount);
        if (!std::isfinite(projected)) throw std::invalid_argument("Fluid callback mass exceeds binary32 range");
        if (projected > amount) projected=std::nextafter(projected,0.0F);
        if (input.kind == 1 && static_cast<double>(projected) != amount)
            throw std::invalid_argument("Injected game mass must be exactly binary32-compatible");
        amount=projected;
    }
    const MacFluidTransfer request{cell,species,input.kind == 1,amount,input.temperature_k};
    auto prepared=mixed_domain(work.configuration,work.physics) ?
        work.owner->with_fluid_transfers(std::span(&request,1),MacFluidTransferTopology::occupied_mixture) :
        work.owner->with_fluid_transfers(std::span(&request,1));
    const auto& packet=prepared.parcels.front(); const bool inject=input.kind == 1;
    work.mass[input.species].add(inject ? packet.mass_kg : -packet.mass_kg);
    MacEnergySum packet_energy;packet_energy.add(packet.internal_energy_j);
    const auto reference=physical_reference_energy(*work.owner,species,packet.mass_kg);
    for (std::size_t k=0;k<reference.size();++k) packet_energy.add(reference.component(k));
    for (std::size_t k=0;k<packet_energy.size();++k)
        work.material.add(inject ? packet_energy.component(k) : packet_energy.component(k).negated());
    work.potential.add(inject ? packet.potential_energy : packet.potential_energy.negated());
    if (!inject) work.kinetic.add(packet.resolved_kinetic_energy.negated());
    MAOniDomainReceipt receipt{0,0,0,input.public_cell,input.species,input.callback,receipt_kind,
        packet.mass_kg,packet.temperature_k,packet_energy.value().require_representable(),packet.resolved_kinetic_energy.require_representable(),
        packet.momentum_x.require_representable(),packet.momentum_y.require_representable()};
    work.owner=std::make_unique<MacClosedWorld>(std::move(prepared.world));
    return receipt;
}
void prepare_transfer(GasWork& work,const MAOniDomainTransfer& input,std::uint32_t receipt_kind) {
    constexpr std::uint32_t shape_mask=0xffff00U;
    const auto radius=(input.flags>>8)&255U,box_height=(input.flags>>16)&255U;
    if (radius==0 && box_height==0) { work.append(prepare_point_transfer(work,input,receipt_kind));return; }
    if (input.struct_size!=sizeof(input) || input.abi_version!=MA_ONI_DOMAIN_TRANSFER_ABI_VERSION || input.reserved ||
        (input.flags&~(shape_mask|1U)) || !(input.flags&1U) || input.kind!=2 || receipt_kind!=2 || input.species>=ids.size() ||
        !admitted_domain_species(work.configuration,work.physics,input.species) || !std::isfinite(input.mass_kg) || input.mass_kg<0 ||
        !std::isfinite(input.temperature_k) || static_cast<double>(static_cast<float>(input.mass_kg))!=input.mass_kg)
        throw std::invalid_argument("Invalid binary32 regional consumption request");
    if (input.session!=work.session || input.expected_frame!=work.frame)
        throw std::logic_error("Regional consumption source session/frame is stale");
    const auto centre=domain_index(input.public_cell,work.configuration);
    const auto grid=work.owner->state().grid;
    // A radius names its centre cell. A rectangle names a footprint whose
    // solids, including the anchor, are skipped like the native loop.
    if (!box_height && grid.walls[centre]) throw std::invalid_argument("Radial consumption centre is a configured solid cell");
    const auto species=work.owner->registry().index(ids[input.species]);
    std::vector<double> amounts;amounts.reserve(work.owner->state().cells.size());
    for (const auto& cell:work.owner->state().cells) amounts.push_back(cell.mixture.masses[species]);
    const auto plan=box_height ? radial::prepare_rectangle(grid.width,grid.height,grid.walls,amounts,centre,radius,box_height,input.mass_kg) :
        radial::prepare(grid.width,grid.height,grid.walls,amounts,centre,radius,input.mass_kg);
    MAOniDomainReceipt combined{0,0,0,input.public_cell,input.species,input.callback,2,plan.mass,0,0,0,0,0};
    MacEnergySum material,kinetic,px,py,temperature_mass;
    for (const auto& allocation:plan.allocations) {
        auto point=input;point.flags=1;point.callback=-1;
        point.public_cell=static_cast<std::uint32_t>(allocation.cell%grid.width+work.configuration.offset_x+
            (allocation.cell/grid.width+work.configuration.offset_y)*work.configuration.native_grid_width);
        point.mass_kg=allocation.mass;
        const auto packet=prepare_point_transfer(work,point,2);
        if (packet.mass_kg!=point.mass_kg) throw std::logic_error("Regional parcel changed its exact callback lattice allocation");
        material.add(packet.internal_energy_j);kinetic.add(packet.kinetic_energy_j);px.add(packet.momentum_x);py.add(packet.momentum_y);
        temperature_mass.add(MacEnergy::from_double(packet.mass_kg).multiplied(packet.temperature_k));
    }
    if (combined.mass_kg>0) combined.temperature_k=temperature_mass.value().shared(1,combined.mass_kg).require_representable();
    combined.internal_energy_j=material.value().require_representable();combined.kinetic_energy_j=kinetic.value().require_representable();
    combined.momentum_x=px.value().require_representable();combined.momentum_y=py.value().require_representable();
    work.append(combined);
}
MAOniDomainActorStatus actor_status() {
    return {sizeof(MAOniDomainActorStatus),1,status.state,static_cast<std::uint32_t>(actors.size()),status.session,
        status.completed_frames,accepted_actor_configurations,actor_steps,consumer_samples,emitter_samples};
}
void prepare_actors(GasWork& work,std::map<std::pair<std::uint32_t,std::int32_t>,Actor>& next,double seconds,
    std::uint64_t& steps,std::uint64_t& consumed,std::uint64_t& emitted) {
    std::vector<std::pair<std::pair<std::uint32_t,std::int32_t>,Actor*>> ordered;
    for (auto& item : next) ordered.emplace_back(item.first,&item.second);
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
        const auto& first=*a.second;const auto& second=*b.second;
        if (!first.identity.owner && !second.identity.owner) return a.first<b.first;
        return std::tuple(first.config.kind,first.identity.owner!=0,first.identity,a.first.second)<
               std::tuple(second.config.kind,second.identity.owner!=0,second.identity,b.first.second);
    });
    for (auto& item : ordered) {
        auto& actor=*item.second;
        const auto& input=actor.config; ++steps;
        MAOniDomainTransfer transfer{64,MA_ONI_DOMAIN_TRANSFER_ABI_VERSION,work.session,work.frame,input.public_cell,input.species,
            input.kind == 1 ? 2U : 1U,1,input.native_handle,0,0,input.temperature_k};
        if (input.kind == 1) {
            if (input.mass_kg == 0) continue;
            const auto cell=domain_index(input.public_cell,work.configuration),species=work.owner->registry().index(ids[input.species]);
            const double available=work.owner->state().cells[cell].mixture.masses[species];
            const double demand=input.mass_kg*seconds+actor.rate_remainder;
            transfer.mass_kg=std::min(demand,available);
            const auto before=work.outbox.size(); prepare_transfer(work,transfer,3);
            actor.rate_remainder=transfer.mass_kg-work.outbox[before].mass_kg; ++consumed;
        } else {
            std::size_t count=0;
            if (input.mass_kg > 0) {
                const auto due=std::floor(actor.elapsed/input.interval_s);
                if (!std::isfinite(due) || due > 256) throw std::length_error("Emitter has too many intervals in one native frame");
                count=static_cast<std::size_t>(due);
                actor.elapsed-=static_cast<double>(count)*input.interval_s;
                actor.elapsed+=seconds;
                if (!std::isfinite(actor.elapsed)) throw std::overflow_error("Emitter elapsed time overflow");
            }
            const auto cell=domain_index(input.public_cell,work.configuration);
            const auto density=work.owner->state().cells[cell].mixture.mass()/work.owner->state().grid.volume_m3();
            const bool blocked=count && density >= input.maximum_gas_density_kg_m3;
            if (count && blocked != actor.blocked) {
                work.append({0,0,0,input.public_cell,input.species,input.native_handle,blocked ? 5U : 6U,0,0,0,0,0,0});
                actor.blocked=blocked;
            }
            if (!count || blocked) {
                work.append({0,0,0,input.public_cell,input.species,input.native_handle,4,0,0,0,0,0,0}); ++emitted;
            } else for (std::size_t i=0;i<count;++i) {
                transfer.mass_kg=input.mass_kg; prepare_transfer(work,transfer,4); ++emitted;
            }
        }
    }
    work.validate_accounts();
}
void validate_domain_geometry(const MAOniDomainConfig& config) {
    if (config.struct_size != sizeof(config) || config.abi_version != MA_ONI_DOMAIN_ABI_VERSION ||
        config.flags != MA_ONI_DOMAIN_SEALED_TEST || config.reserved || config.scenario > 6 ||
        config.width < 2 || config.height < 2 || config.width > 16 || config.height > 16 ||
        config.native_grid_width < config.width+2 || config.offset_x == 0 || config.offset_y == 0 ||
        static_cast<std::uint64_t>(config.offset_x)+config.width >= config.native_grid_width ||
        (static_cast<std::uint64_t>(config.offset_y)+config.height+2)*(static_cast<std::uint64_t>(config.native_grid_width)+2) > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid sealed native domain geometry, model or opt-in");
}
actor_checkpoint::State saved_actor(const Actor& actor) {
    const auto& c=actor.config;
    actor_checkpoint::State result{actor.identity,c.kind,c.public_cell,c.species,c.mass_kg,c.interval_s,c.temperature_k,
        c.maximum_gas_density_kg_m3,actor.elapsed,actor.rate_remainder,actor.blocked};
    actor_checkpoint::validate(result);return result;
}
Actor restored_actor(const actor_checkpoint::State& saved,std::int32_t handle,std::uint64_t session,std::uint64_t frame) {
    return {{sizeof(MAOniDomainActor),1,session,frame,handle,saved.kind,saved.cell,saved.species,0,0,
        saved.mass,saved.interval,saved.temperature,saved.maximum_density},saved.elapsed,saved.remainder,saved.blocked,saved.identity};
}
actor_checkpoint::Image decoded_actor_image(const MAOniDomainConfig& config,std::span<const std::uint8_t> image) {
    auto result=actor_checkpoint::decode(image);
    if (!actor_checkpoint::same_geometry(config,result.geometry)) throw std::invalid_argument("Durable actor geometry differs from its saved domain");
    for (const auto& actor : result.actors) {
        (void)domain_index(actor.cell,config);
        if (!admitted_species(config.scenario,actor.species)) throw std::invalid_argument("Durable actor phase differs from its saved domain");
    }
    return result;
}
std::unique_ptr<MacClosedWorld> restored_domain_owner(const MAOniDomainConfig& config,std::span<const std::uint8_t> image,double saved_time) {
    validate_domain_geometry(config);
    if (image.empty() || !std::isfinite(saved_time) || saved_time<0) throw std::invalid_argument("Invalid saved native domain source");
    actor_checkpoint::Image actor_image;
    if (actor_checkpoint::has_magic(image)) { actor_image=decoded_actor_image(config,image);image=actor_image.physical; }
    const auto registry=domain_registry(config.scenario); const auto eos=model(registry,config.scenario);
    auto candidate=std::make_unique<MacClosedWorld>(MacClosedWorld::from_checkpoint(registry,eos,image));
    const auto& state=candidate->state(); const auto& grid=state.grid;
    if (candidate->time_s()!=saved_time || grid.width!=config.width || grid.height!=config.height || grid.spacing_m!=1 || grid.depth_m!=1 ||
        grid.periodic_x || grid.periodic_y || std::any_of(grid.walls.begin(),grid.walls.end(),[](auto value) { return value!=0; }) ||
        candidate->settings().gravity_m_per_s2!=(gravity_scenario(config.scenario) ? 9.81 : 0) ||
        candidate->settings().vacuum!=MacVacuumRepresentation::empty_cells || !state.occupied_volumes_m3.empty() ||
        (mixed_scenario(config.scenario) && candidate->settings().thermal_conduction!=(config.scenario!=4)) ||
        candidate->settings().viscosity.shear_pa_s!=(config.scenario==6 ? .5 : 0) ||
        candidate->settings().viscosity.bulk_pa_s!=(config.scenario==6 ? .25 : 0))
        throw std::invalid_argument("Physical checkpoint clock, grid or gravity differs from its native domain configuration");
    const auto water=registry.index("water");
    for (const auto& cell : state.cells) {
        if (!cell.passive_quantities.empty()) throw std::invalid_argument("Saved native domain requires a passive-metadata adapter");
        for (std::size_t k=0;k<registry.size();++k)
            if (cell.mixture.masses[k]!=0 && !mixed_scenario(config.scenario) && (liquid_scenario(config.scenario)!=(k==water)))
                throw std::invalid_argument("Saved native domain phase differs from its declared scenario");
    }
    (void)project(*candidate,config,MAOniDomainPhysics{}); (void)energy(*candidate);
    return candidate;
}
// Configured admission. Size: one 64x64 gravity/conduction/viscosity frame
// raised peak RSS by about 21 KiB per cell (85 MiB); a 1024-cell frame without
// viscosity, 13 KiB per cell. Retained owner, publication (96 B) and snapshot
// (about 160 B) copies are a few hundred bytes per cell. Every count fits
// uint32 and the native index stays below INT_MAX (guard below).
void validate_configured_layout(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,std::span<const std::uint8_t> mask) {
    if (config.struct_size != sizeof(config) || config.abi_version != MA_ONI_DOMAIN_ABI_VERSION ||
        config.flags != MA_ONI_DOMAIN_SEALED_TEST || config.reserved || !configured_scenario(config.scenario) ||
        config.width < 2 || config.height < 2 || config.width > MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_SIDE ||
        config.height > MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_SIDE ||
        static_cast<std::uint64_t>(config.width)*config.height > MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS ||
        config.native_grid_width < config.width+2 || config.offset_x == 0 || config.offset_y == 0 ||
        static_cast<std::uint64_t>(config.offset_x)+config.width >= config.native_grid_width ||
        // Bounding the pitch first keeps the uint64 product below 2^64; without
        // it, offset_y+height+2 == pitch == 2^32 wraps the product to zero.
        config.native_grid_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()-2) ||
        (static_cast<std::uint64_t>(config.offset_y)+config.height+2)*(static_cast<std::uint64_t>(config.native_grid_width)+2) > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid configured native domain geometry, size or opt-in");
    const auto flags=physics.flags;
    if (physics.struct_size != sizeof(physics) || physics.abi_version != MA_ONI_DOMAIN_CONFIGURED_ABI_VERSION || physics.reserved ||
        (flags&~configured_physics_flags) || ((flags&MA_ONI_DOMAIN_PHYSICS_MIXTURE) && (flags&MA_ONI_DOMAIN_PHYSICS_LIQUID)) ||
        ((flags&MA_ONI_DOMAIN_PHYSICS_UNRESOLVED_GAS) && !(flags&MA_ONI_DOMAIN_PHYSICS_VACUUM)))
        throw std::invalid_argument("Configured native domain physics ABI, flags or phase admission differ");
    const auto g=physics.gravity_m_per_s2;
    if ((flags&MA_ONI_DOMAIN_PHYSICS_GRAVITY) ? !(std::isfinite(g) && g>0 && g<=100) : !positive_zero(g))
        throw std::invalid_argument("Configured native domain gravity is outside 0<g<=100 m/s2 or not exactly disabled");
    const auto shear=physics.shear_viscosity_pa_s,bulk=physics.bulk_viscosity_pa_s;
    const auto admitted=[](double value) { return std::isfinite(value) && (value>0 || positive_zero(value)) && value<=1000; };
    if ((flags&MA_ONI_DOMAIN_PHYSICS_VISCOSITY) ? !(admitted(shear) && admitted(bulk) && (shear>0 || bulk>0)) :
        !(positive_zero(shear) && positive_zero(bulk)))
        throw std::invalid_argument("Configured native domain viscosity is outside 0..1000 Pa s or not exactly disabled");
    if (mask.size()!=static_cast<std::size_t>(config.width)*config.height ||
        std::any_of(mask.begin(),mask.end(),[](auto value) { return value>1; }) ||
        std::none_of(mask.begin(),mask.end(),[](auto value) { return value==0; }))
        throw std::invalid_argument("Configured native solid mask must be binary, cover every cell and retain a fluid cell");
}
// The enclosed MADCFG01 section carries physics and mask; actor-header
// geometry alone cannot identify a configured owner.
actor_checkpoint::Image decoded_configured_actor_image(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,
    std::span<const std::uint8_t> mask,std::span<const std::uint8_t> image) {
    auto result=actor_checkpoint::decode(image);
    if (!actor_checkpoint::same_geometry(config,result.geometry) || !configured_domain::has_magic(result.physical) ||
        !configured_domain::same_configuration(configured_domain::decode(result.physical),config,physics,mask))
        throw std::invalid_argument("Durable actor geometry, physics or solid mask differs from its saved configured domain");
    for (const auto& actor : result.actors) {
        if (mask[domain_index(actor.cell,config)]) throw std::invalid_argument("Durable actor targets a configured solid cell");
        if (!admitted_domain_species(config,physics,actor.species)) throw std::invalid_argument("Durable actor phase differs from its saved domain");
    }
    return result;
}
std::unique_ptr<MacClosedWorld> restored_configured_owner(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,
    std::span<const std::uint8_t> mask,std::span<const std::uint8_t> image,double saved_time) {
    validate_configured_layout(config,physics,mask);
    if (image.empty() || !std::isfinite(saved_time) || saved_time<0) throw std::invalid_argument("Invalid saved configured native domain source");
    actor_checkpoint::Image actor_image;
    if (actor_checkpoint::has_magic(image)) { actor_image=decoded_configured_actor_image(config,physics,mask,image);image=actor_image.physical; }
    const auto saved=configured_domain::decode(image);
    if (!configured_domain::same_configuration(saved,config,physics,mask))
        throw std::invalid_argument("Configured checkpoint geometry, physics or solid mask differs from its native domain configuration");
    const auto registry=domain_registry(0); const auto eos=model(registry,0);
    auto candidate=std::make_unique<MacClosedWorld>(MacClosedWorld::from_checkpoint(registry,eos,saved.physical));
    const auto& state=candidate->state(); const auto& grid=state.grid;
    const auto& actual=candidate->settings(); const auto expected=configured_settings(physics);
    if (candidate->time_s()!=saved_time || grid.width!=config.width || grid.height!=config.height || grid.spacing_m!=1 || grid.depth_m!=1 ||
        grid.periodic_x || grid.periodic_y || !std::equal(grid.walls.begin(),grid.walls.end(),mask.begin(),mask.end()) ||
        !same_bits(actual.gravity_m_per_s2,expected.gravity_m_per_s2) || actual.thermal_conduction!=expected.thermal_conduction ||
        actual.vacuum!=expected.vacuum || actual.reconstruction!=expected.reconstruction || actual.phase_change!=expected.phase_change ||
        actual.subcell_void!=expected.subcell_void ||
        (!void_domain(physics) && !state.occupied_volumes_m3.empty()) ||
        !same_bits(actual.viscosity.shear_pa_s,expected.viscosity.shear_pa_s) || !same_bits(actual.viscosity.bulk_pa_s,expected.viscosity.bulk_pa_s))
        throw std::invalid_argument("Configured physical checkpoint clock, grid, solid mask or physics differs from its native domain configuration");
    const auto water=registry.index("water");
    for (std::size_t i=0;i<state.cells.size();++i) {
        const auto& cell=state.cells[i];
        if (!cell.passive_quantities.empty()) throw std::invalid_argument("Saved native domain requires a passive-metadata adapter");
        if (grid.walls[i]) continue; // The solver image already requires empty solids.
        if (!(cell.mixture.mass()>0)) {
            if (!void_domain(physics)) throw std::invalid_argument("Saved configured domain contains a vacuum fluid cell");
            continue;
        }
        for (std::size_t k=0;k<registry.size();++k)
            if (cell.mixture.masses[k]!=0 && !mixed_domain(config,physics) && liquid_domain(config,physics)!=(k==water))
                throw std::invalid_argument("Saved configured domain phase differs from its declared physics");
    }
    (void)project(*candidate,config,physics); (void)energy(*candidate);
    return candidate;
}
std::unique_ptr<MacClosedWorld> configured_owner_from_cells(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,
    std::span<const std::uint8_t> mask,std::span<const MAOniDomainInitialCell> initial_cells) {
    const auto registry=domain_registry(0); const auto eos=model(registry,0);
    MacTransportState state;
    state.grid={config.width,config.height,1,1,false,false,std::vector<std::uint8_t>(mask.begin(),mask.end())};
    state.cells.reserve(mask.size()); std::vector<double> volumes; volumes.reserve(mask.size());
    for (std::size_t index=0;index<mask.size();++index) {
        const auto& input=initial_cells[index];
        if (input.struct_size!=sizeof(input) || input.abi_version!=1) throw std::invalid_argument("Invalid configured initial inventory cell ABI");
        if (mask[index]) {
            if (input.temperature_k!=0 || std::any_of(std::begin(input.masses),std::end(input.masses),[](double mass) { return mass!=0; }))
                throw std::invalid_argument("Configured solid cell has a nonzero initial inventory record");
            state.cells.push_back(MacMaterialCell{MixtureInventory(registry.size()),{}}); volumes.push_back(0);
            continue;
        }
        if (!std::isfinite(input.temperature_k) || input.temperature_k<0) throw std::invalid_argument("Invalid configured initial fluid temperature");
        std::vector<double> values(registry.size()); bool occupied=false;
        for (std::size_t k=0;k<ids.size();++k) {
            const auto mass=input.masses[k];
            if (!std::isfinite(mass) || mass<0 || (mass!=0 && !admitted_domain_species(config,physics,static_cast<std::uint32_t>(k))))
                throw std::invalid_argument("Configured initial inventory mass or phase is unsupported");
            values[registry.index(ids[k])]=mass; occupied|=mass>0;
        }
        if (!occupied) {
            if (!void_domain(physics)) throw std::invalid_argument("Configured initial inventory does not admit vacuum fluid cells");
            state.cells.push_back(MacMaterialCell{MixtureInventory(registry.size()),{}}); volumes.push_back(0); continue;
        }
        if (!(input.temperature_k>0)) throw std::invalid_argument("Invalid configured initial fluid temperature");
        // Void phase: a liquid-only cell holding less than a full cell rests at
        // zero pressure in its nominal liquid volume.
        double held=1;
        if (void_domain(physics)) {
            double nominal=0; bool gas=false;
            for (std::size_t k=0;k<registry.size();++k) if (values[k]!=0) {
                if (registry[k].phase==Phase::gas) gas=true;
                else for (const auto& m : eos->reference().materials()) if (m.id==registry[k].id) nominal+=values[k]/m.zero_pressure_density_kg_per_m3;
            }
            if (!gas && nominal<1) held=nominal;
        }
        state.cells.push_back({eos->initialize_at_temperature(values,input.temperature_k,held).mixture,{}}); volumes.push_back(held);
    }
    if (void_domain(physics)) state.occupied_volumes_m3=std::move(volumes); // cleared by the owner when every fluid cell is full
    const auto inertia=mac_face_inertias(registry,state,*eos);
    state.momentum_x.resize(inertia.x.size()); state.momentum_y.resize(inertia.y.size());
    return std::make_unique<MacClosedWorld>(registry,eos,std::move(state),configured_settings(physics));
}
std::map<int,MAOniDomainSolidHeatCell> checked_solid_heat(const MAOniDomainConfig& layout,
    std::span<const std::uint8_t> walls,std::span<const MAOniDomainSolidHeatCell> inputs,
    std::uint64_t session,std::uint64_t frame,bool startup=false,std::uint64_t assigned_session=0) {
    if(inputs.empty() || inputs.size()>128) throw std::invalid_argument("Invalid solid boundary registration count");
    std::map<int,MAOniDomainSolidHeatCell> candidate;
    for(std::size_t index=0;index<inputs.size();++index) {
        auto input=inputs[index];
        if(input.struct_size!=sizeof(input) || input.abi_version!=1 || input.native_insulation>255 || input.native_element_index>65535 ||
            !std::isfinite(input.mass_kg) || input.mass_kg<=0 || !std::isfinite(input.specific_heat_kj_per_kg_k) || input.specific_heat_kj_per_kg_k<=0 ||
            !std::isfinite(input.conductivity_w_per_m_k) || input.conductivity_w_per_m_k<0)
            throw std::invalid_argument("Invalid native solid boundary facts");
        if(input.session!=session || input.expected_frame!=frame) {
            if(startup) throw std::invalid_argument("Initial solid boundary requires an unbound session and frame");
            throw std::logic_error("Solid boundary registration source changed");
        }
        if(startup) input.session=assigned_session;
        const auto xx=input.public_cell%layout.native_grid_width,yy=input.public_cell/layout.native_grid_width;
        const auto x0=layout.offset_x,y0=layout.offset_y,x1=x0+layout.width,y1=y0+layout.height;
        const bool face=((xx+1==x0 || xx==x1) && yy>=y0 && yy<y1) || ((yy+1==y0 || yy==y1) && xx>=x0 && xx<x1);
        if(!face || xx>=layout.native_grid_width || yy==UINT32_MAX || xx==0 || yy==0)
            throw std::invalid_argument("Native solid is not an outside face of the owned rectangle");
        // Interior fluid/solid faces stay adiabatic. An outside face that
        // meets a configured solid is a native solid/solid pair, never owned.
        const auto inside_x=xx+1==x0 ? x0 : xx==x1 ? x1-1 : xx,inside_y=yy+1==y0 ? y0 : yy==y1 ? y1-1 : yy;
        if(walls[inside_x-x0+(inside_y-y0)*layout.width])
            throw std::invalid_argument("Native solid boundary face touches a configured solid, not an owned fluid cell");
        const auto native=static_cast<std::uint64_t>(xx)+1+(static_cast<std::uint64_t>(yy)+1)*(layout.native_grid_width+2ULL);
        if(native>INT32_MAX || !candidate.emplace(static_cast<int>(native),input).second)
            throw std::invalid_argument("Duplicate or out-of-range native solid boundary");
    }
    return candidate;
}

// Caller holds both locks and the arithmetic scope, with the previous session
// off. Every allocation and check precedes the first published global.
void activate_domain(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,std::unique_ptr<MacClosedWorld> candidate,
    std::span<const std::uint8_t> image,std::uint64_t thread,std::map<ActorIdentityKey,actor_checkpoint::State> restored_actors,
    DomainSolidHeatStartup* solid_heat=nullptr) {
    auto initial_snapshot=image.empty() ? physical_image(*candidate,config,physics) : std::vector<std::uint8_t>(image.begin(),image.end());
    auto cells=project(*candidate,config,physics); const auto reported=reported_cell(*candidate);
    const auto totals=masses(*candidate); const auto total_energy=energy(*candidate); const auto next_potential=potential(*candidate);
    MAOniDomainStatus next{}; next.struct_size=sizeof(next); next.abi_version=MA_ONI_DOMAIN_ABI_VERSION;
    if (session_sequence==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native domain session range exhausted");
    auto initial_solids=solid_heat ? checked_solid_heat(config,candidate->state().grid.walls,
        solid_heat->cells,0,0,true,session_sequence+1) : std::map<int,MAOniDomainSolidHeatCell>{};
    published_frames::reset();
    next.state=1; next.cells=static_cast<std::uint32_t>(cells.size()); next.session=++session_sequence;
    next.time_seconds=candidate->time_s(); geometry=config; physics_settings=physics;
    const auto& walls=candidate->state().grid.walls;
    for (std::size_t i=0;i<solid_cells.size();++i) solid_cells[i].store(i<walls.size() ? walls[i] : 0,std::memory_order_relaxed);
    next.start_thread=thread; next.initial_oxygen_kg=cells[reported].masses[1]; next.current_oxygen_kg=next.initial_oxygen_kg;
    next.pressure_pa=cells[reported].pressure; next.liquid_mass_kg=cells[reported].masses[3];
    world=std::move(candidate); publication=std::move(cells); initial_masses=totals; initial_energy=total_energy;
    status=next;
    pending_world.reset(); accepted_heat_batches=committed_heat_batches=0;
    pending_heat_batches=pending_heat_edits=0; committed_heat={}; pending_heat={};
    committed_mass={}; pending_mass={}; committed_material={}; pending_material={}; committed_kinetic={}; pending_kinetic={};
    accepted_transfer_batches=committed_transfer_batches=receipt_sequence=acknowledged_receipt=0;
    pending_transfer_batches=0; pending_receipts.clear(); receipts.clear();
    pending_inputs.clear();completed_inputs.clear();completed_snapshot=std::move(initial_snapshot);pending_input_flags=0;
    completed_audit.reset();audit_full_serializations=audit_journal_serializations=0;
    actors.clear(); accepted_actor_configurations=actor_steps=consumer_samples=emitter_samples=0;
    pending_actor_restore=std::move(restored_actors);
    thermal_stages=thermal_contacts=thermal_iterations=unresolved_thermal_updates=thermal_equilibrium_intervals=0;
    thermal_redistribution={}; thermal_entropy={};
    diffusion_progress={};
    viscous_stages=viscous_modes=unresolved_viscous_heat=unresolved_viscous_momentum=0;
    viscous_dissipation={};viscous_stored_heat={};viscous_entropy={};viscous_wall_x={};viscous_wall_y={};
    subcell_void_events=0;subcell_void_entropy={};
    pending_chunk_heat={};committed_chunk_heat={};pending_solid_heat={};committed_solid_heat={};
    solid_heat_cells=std::move(initial_solids);solid_heat_contacts.clear();completed_solid_cell={};
    committed_potential={}; pending_potential={}; body_impulse={}; gravity_internal_work={};
    initial_potential=next_potential; maximum_energy_residual=0;
    for (auto& value : suppressed) value=0;
    if(solid_heat) {
        solid_heat->status=status;
        solid_heat->heat={};solid_heat->heat.struct_size=sizeof(solid_heat->heat);solid_heat->heat.abi_version=1;
        solid_heat->heat.state=1;solid_heat->heat.session=status.session;
    }
    enabled.store(true,std::memory_order_release);
}

}

void start_domain(const MAOniDomainConfig& config,std::uint64_t thread,std::span<const std::uint8_t> image,double saved_time,
    std::span<const MAOniDomainInitialCell> initial_cells,DomainSolidHeatStartup* solid_heat) {
    validate_domain_geometry(config);
    if (!initial_cells.empty() && (initial_cells.size()!=static_cast<std::size_t>(config.width)*config.height || !image.empty() || saved_time!=0))
        throw std::invalid_argument("Initial native inventory must cover exactly one new domain");
    std::lock_guard frame_lock(frame_mutex);
    std::lock_guard lock(state_mutex);
    if (enabled.load(std::memory_order_acquire)) throw std::logic_error("A sealed native domain is already active");
    const ArithmeticScope arithmetic;
    const auto registry=domain_registry(config.scenario); const auto eos=model(registry,config.scenario);
    MacTransportState state;
    state.grid={config.width,config.height,1,1,false,false,std::vector<std::uint8_t>(config.width*config.height)};
    for (std::uint32_t y=0;y<config.height;++y) for (std::uint32_t x=0;x<config.width;++x) {
        std::vector<double> values(registry.size());
        double temperature=300;
        if (!initial_cells.empty()) {
            const auto& input=initial_cells[x+y*config.width];
            if (input.struct_size!=sizeof(input) || input.abi_version!=1 || !std::isfinite(input.temperature_k) || input.temperature_k<=0)
                throw std::invalid_argument("Invalid initial native inventory cell ABI or temperature");
            bool occupied=false;
            for (std::size_t k=0;k<ids.size();++k) {
                const auto mass=input.masses[k];
                if (!std::isfinite(mass) || mass<0 || (mass!=0 && !admitted_species(config.scenario,static_cast<std::uint32_t>(k))))
                    throw std::invalid_argument("Initial native inventory mass or phase is unsupported");
                values[registry.index(ids[k])]=mass; occupied|=mass>0;
            }
            if (!occupied) throw std::invalid_argument("Initial native inventory does not admit vacuum cells");
            temperature=input.temperature_k;
        }
        else if (liquid_scenario(config.scenario)) values[registry.index("water")]=1100;
        else {
            const double oxygen=.18+.12*static_cast<double>(x)/static_cast<double>(config.width-1);
            const double pressure=100000+1000*static_cast<double>(x)/static_cast<double>(config.width-1);
            const double coefficient=gas_constant*((1-oxygen)/registry[registry.index("nitrogen")].molar_mass+
                oxygen/registry[registry.index("oxygen")].molar_mass+1e-28/registry[registry.index("helium")].molar_mass);
            const double water_fraction=mixed_scenario(config.scenario) ? .2+.05*static_cast<double>(x)/static_cast<double>(config.width-1) : 0;
            const double gas=(1-water_fraction)*pressure/(coefficient*300);
            values[registry.index("nitrogen")]=(1-oxygen)*gas; values[registry.index("oxygen")]=oxygen*gas;
            values[registry.index("helium")]=1e-28*gas;
            if (mixed_scenario(config.scenario)) values[registry.index("water")]=water_fraction*1000*(1+pressure/2.2e9);
        }
        state.cells.push_back({eos->initialize_at_temperature(values,temperature,1).mixture,{}});
    }
    const auto inertia=mac_face_inertias(registry,state,*eos);
    state.momentum_x.resize(inertia.x.size()); state.momentum_y.resize(inertia.y.size());
    if (config.scenario==6 && initial_cells.empty())
        for (std::uint32_t y=0;y<config.height;++y) for (std::uint32_t x=1;x<config.width;++x)
            state.momentum_x[state.grid.x_face(x,y)]=inertia.x[state.grid.x_face(x,y)]*(y%2==0 ? .5 : -.5);
    MacClosedSettings settings; settings.maximum_timestep_s=.01; settings.thermal_conduction=config.scenario!=4;
    settings.numerical_kernel=MacNumericalKernel::production;
    if (config.scenario==6) settings.viscosity={.5,.25};
    settings.gravity_m_per_s2=gravity_scenario(config.scenario) ? 9.81 : 0;
    auto candidate=std::make_unique<MacClosedWorld>(registry,eos,std::move(state),settings);
    // All allocations and checks happen before publishing the activation flag.
    if (!image.empty()) candidate=restored_domain_owner(config,image,saved_time);
    std::map<ActorIdentityKey,actor_checkpoint::State> restored_actors;
    if (actor_checkpoint::has_magic(image)) {
        const auto saved=decoded_actor_image(config,image);
        for (const auto& actor : saved.actors) restored_actors.emplace(actor.key(),actor);
    }
    activate_domain(config,MAOniDomainPhysics{},std::move(candidate),image,thread,std::move(restored_actors),solid_heat);
}
void start_configured_domain(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,std::span<const std::uint8_t> mask,
    std::uint64_t thread,std::span<const std::uint8_t> image,double saved_time,std::span<const MAOniDomainInitialCell> initial_cells) {
    validate_configured_layout(config,physics,mask);
    if (image.empty() ? (initial_cells.size()!=mask.size() || saved_time!=0) : !initial_cells.empty())
        throw std::invalid_argument("Configured native domain needs exactly one complete initial inventory or one saved image");
    std::lock_guard frame_lock(frame_mutex);
    std::lock_guard lock(state_mutex);
    if (enabled.load(std::memory_order_acquire)) throw std::logic_error("A sealed native domain is already active");
    const ArithmeticScope arithmetic;
    std::unique_ptr<MacClosedWorld> candidate;
    std::map<ActorIdentityKey,actor_checkpoint::State> restored_actors;
    if (image.empty()) candidate=configured_owner_from_cells(config,physics,mask,initial_cells);
    else {
        candidate=restored_configured_owner(config,physics,mask,image,saved_time);
        if (actor_checkpoint::has_magic(image))
            for (const auto& actor : decoded_configured_actor_image(config,physics,mask,image).actors) restored_actors.emplace(actor.key(),actor);
    }
    activate_domain(config,physics,std::move(candidate),image,thread,std::move(restored_actors));
}
void validate_domain_checkpoint(const MAOniDomainConfig& config,std::span<const std::uint8_t> image,double saved_time) {
    const ArithmeticScope arithmetic; (void)restored_domain_owner(config,image,saved_time);
}
void validate_configured_domain_checkpoint(const MAOniDomainConfig& config,const MAOniDomainPhysics& physics,
    std::span<const std::uint8_t> mask,std::span<const std::uint8_t> image,double saved_time) {
    const ArithmeticScope arithmetic; (void)restored_configured_owner(config,physics,mask,image,saved_time);
}
void copy_domain_configuration(std::uint64_t session,MAOniDomainConfig& config,MAOniDomainPhysics& physics,std::vector<std::uint8_t>& mask) {
    std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || !world || session!=status.session)
        throw std::logic_error("Configuration copy requires the current active session");
    if (!configured_scenario(geometry.scenario)) throw std::invalid_argument("Legacy native domain scenarios have no configured physics or solid mask");
    auto next=world->state().grid.walls;config=geometry;physics=physics_settings;mask=std::move(next);
}
void stop_domain() noexcept {
    // Stopping while the native sim runs would expose scalar projections to
    // original fluid physics. Only shutdown_domain is allowed to release it.
    std::lock_guard lock(state_mutex);
    if (status.state == 1) status.state=2;
}
void shutdown_domain() noexcept {
    std::lock_guard lock(state_mutex);
    // A later simulator can reuse a GameData address. Unacquired bindings and
    // cached sources end with this owner; acquired leases remain valid.
    try { published_frames::reset(); } catch (...) {}
    enabled.store(false,std::memory_order_release);
    if (status.state == 1) status.state=2;
    world.reset();
    pending_world.reset(); pending_heat_batches=pending_heat_edits=0; pending_heat={};
    pending_transfer_batches=0; pending_receipts.clear(); pending_mass={}; pending_material={}; pending_kinetic={};
    pending_inputs.clear();completed_inputs.clear();completed_snapshot.clear();pending_input_flags=0;
    completed_audit.reset();audit_full_serializations=audit_journal_serializations=0;
    actors.clear();
    pending_chunk_heat={};committed_chunk_heat={};pending_solid_heat={};committed_solid_heat={};
    solid_heat_cells.clear();solid_heat_contacts.clear();completed_solid_cell={};
    pending_actor_restore.clear();
    ma::experimental::interval_detail::retire_block_executor();
}
bool domain_active() noexcept { return enabled.load(std::memory_order_acquire); }
std::mutex& domain_frame_mutex() noexcept { return frame_mutex; }
MAOniDomainSpecies domain_species(std::uint32_t index) {
    if (index >= ids.size()) throw std::invalid_argument("Unknown sealed-domain species index");
    const ArithmeticScope arithmetic;
    const auto registry=SpeciesRegistry::standard();
    const auto& species=registry[registry.index(ids[index])];
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
    // Configured sessions own fluid cells only (libSimDLL 744825 evidence in
    // docs/native-configured-domain.md). Native liquid/pressure entries never
    // select a solid; solid pairs keep native conduction, and a fluid/solid
    // pair reaches the solid-heat bridge with exactly one owned endpoint. The
    // post-process solid branch is a no-op unless the element is unstable, and
    // transitions run for every cell; a melt or falling solid would create
    // fluid inside the owner, so those entries stay rectangle-wide.
    if (configured_scenario(geometry.scenario) && hook!=MA_ONI_POST_PROCESS && hook!=MA_ONI_STATE_TRANSITION &&
        hook!=MA_ONI_LOAD_TRANSITION && hook!=MA_ONI_SUBLIMATION &&
        solid_cells[x-geometry.offset_x+(y-geometry.offset_y)*geometry.width].load(std::memory_order_relaxed)) return false;
    if (hook < suppressed.size()) suppressed[hook].fetch_add(1,std::memory_order_relaxed);
    return true;
}
void advance_domain(std::uint64_t ticket,double seconds,std::uint64_t thread) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    std::lock_guard lock(state_mutex);
    if (status.state != 1) return;
    try {
        const ArithmeticScope arithmetic;
        if (!pending_actor_restore.empty()) throw std::runtime_error("Saved fluid actors have not all rebound to their live objects");
        if (!ticket || ticket <= status.native_ticket || !std::isfinite(seconds) || seconds <= 0 || seconds > 1)
            throw std::runtime_error("Native frame ticket or timestep is invalid");
        if (thread == status.start_thread) throw std::runtime_error("Custom solver did not execute on a native worker");
        GasWork work; auto next_actors=actors;
        auto next_steps=actor_steps,next_consumed=consumer_samples,next_emitted=emitter_samples;
        prepare_actors(work,next_actors,seconds,next_steps,next_consumed,next_emitted);
        auto result=advance_owned_world(*work.owner,seconds,MacExecutionPolicy::production);
        auto cells=project(result.world,geometry,physics_settings); auto next=status;
        const auto reported=reported_cell(result.world);
        const auto totals=masses(result.world);
        for (std::size_t i=0;i<totals.size();++i) {
            const auto delta=combined_heat(committed_mass[i],work.mass[i]).value().require_representable();
            const auto expected=initial_masses[i]+delta;
            const auto scale=std::abs(initial_masses[i])+std::abs(delta);
            if (scale == 0) {
                if (totals[i] != 0) throw std::runtime_error("Native domain created an unaccounted constituent");
            } else next.maximum_relative_mass_error=std::max(next.maximum_relative_mass_error,
                static_cast<double>(std::abs(totals[i]-expected)/scale));
        }
        auto next_heat=combined_heat(committed_heat,pending_heat);
        auto next_material=combined_heat(committed_material,work.material), next_kinetic=combined_heat(committed_kinetic,work.kinetic);
        auto next_potential=combined_heat(committed_potential,work.potential);
        auto external_energy=combined_heat(combined_heat(combined_heat(next_heat,next_material),next_kinetic),next_potential);
        const auto heat_projection=external_energy.value().require_representable();
        const auto expected_energy=initial_energy+heat_projection;
        const auto energy_scale=std::abs(initial_energy)+std::abs(heat_projection);
        const auto energy_residual=std::abs(energy(result.world)-expected_energy);
        next.maximum_relative_energy_error=std::max(next.maximum_relative_energy_error,static_cast<double>(energy_residual/energy_scale));
        if (next.maximum_relative_mass_error > 1e-8 || next.maximum_relative_energy_error > 1e-6)
            throw std::runtime_error("Native domain exceeded its mass/energy drift bound");
        if (result.report.exact_equilibrium_intervals>result.report.substeps ||
            result.report.exact_equilibrium_intervals>std::numeric_limits<std::uint64_t>::max()-thermal_equilibrium_intervals)
            throw std::overflow_error("Native thermal equilibrium counter overflow or inconsistent report");
        const auto next_thermal_equilibrium=thermal_equilibrium_intervals+result.report.exact_equilibrium_intervals;
        next.substeps+=result.report.substeps; next.rejected_attempts+=result.report.rejected_attempts;
        ++next.completed_frames; next.native_ticket=ticket; next.time_seconds=result.world.time_s();
        next.last_timestep_seconds=seconds; next.solver_thread=thread;
        next.current_oxygen_kg=cells[reported].masses[1]; next.pressure_pa=cells[reported].pressure;
        next.liquid_mass_kg=cells[reported].masses[3];
        auto next_receipts=receipts;
        next_receipts.reserve(receipts.size()+work.outbox.size());
        for (auto receipt : work.outbox) { receipt.completed_frame=next.completed_frames; next_receipts.push_back(receipt); }
        auto next_mass=committed_mass;
        for (std::size_t i=0;i<ids.size();++i) next_mass[i]=combined_heat(committed_mass[i],work.mass[i]);
        auto owner=std::make_shared<const MacClosedWorld>(std::move(result.world));
        auto next_redistribution=thermal_redistribution,next_entropy=thermal_entropy;
        if (result.report.subcell_void_events>std::numeric_limits<std::uint64_t>::max()-subcell_void_events)
            throw std::overflow_error("Native unresolved gas expansion counter overflow");
        const auto next_subcell_events=subcell_void_events+result.report.subcell_void_events;
        auto next_subcell_entropy=subcell_void_entropy;next_subcell_entropy.add(result.report.subcell_void_entropy_j_per_k);
        (void)next_subcell_entropy.value().require_representable();
        auto next_viscous_dissipation=viscous_dissipation,next_viscous_stored_heat=viscous_stored_heat,next_viscous_entropy=viscous_entropy;
        auto next_viscous_wall_x=viscous_wall_x,next_viscous_wall_y=viscous_wall_y;
        next_viscous_dissipation.add(result.report.viscous_heat);next_viscous_stored_heat.add(result.report.viscous_stored_heat);
        next_viscous_entropy.add(result.report.viscous_entropy_lower_bound_j_per_k);
        next_viscous_wall_x.add(result.report.viscous_wall_impulse_x);next_viscous_wall_y.add(result.report.viscous_wall_impulse_y);
        for (const auto* value:{&next_viscous_dissipation,&next_viscous_stored_heat,&next_viscous_entropy,&next_viscous_wall_x,&next_viscous_wall_y})
            if (!std::isfinite(value->value().project().value)) throw std::overflow_error("Native viscosity diagnostic exceeds its scalar range");
        auto next_impulse=body_impulse,next_gravity_work=gravity_internal_work;
        // Accumulate the full-range operands: the scalar projections round
        // below-range impulse/work to zero, which would pass the check below.
        next_impulse.add(result.report.full_body_impulse_y); next_gravity_work.add(result.report.gravity_internal_change_wide_j);
        (void)next_impulse.value().require_representable(); (void)next_gravity_work.value().require_representable();
        next_redistribution.add(result.report.conductive_redistribution_j); next_entropy.add(result.report.conductive_entropy_j_per_k);
        if (!std::isfinite(next_redistribution.value().project().value) || !std::isfinite(next_entropy.value().project().value))
            throw std::overflow_error("Native conduction diagnostic exceeds its scalar range");
        // Pin the two immutable endpoints and the accepted inputs. Ordinary
        // frames do not serialize/hash either world. Capture readers build
        // the exact legacy images on demand under the publication lock.
        DeferredFrameAudit next_audit;next_audit.source=world;next_audit.physics=physics_settings;
        auto input_flags=pending_input_flags|frame_inputs::production_advance;
        if (!actors.empty()) {
            if (std::any_of(actors.begin(),actors.end(),[](const auto& item) {return !item.second.identity.owner;})) input_flags|=2U;
            else {
                for (const auto& [key,actor]:actors) { (void)key;next_audit.source_actors.push_back(saved_actor(actor)); }
            }
        }
        next_audit.journal={input_flags,status.session,status.completed_frames,next.completed_frames,
            world->time_s(),seconds,geometry,{},pending_inputs};
        if ((input_flags&2U)==0) {
            next_audit.completed=owner;
            for (const auto& [key,actor]:next_actors) {(void)key;next_audit.completed_actors.push_back(saved_actor(actor));}
        }
        auto next_chunk_heat=combined_chunk_heat(committed_chunk_heat,pending_chunk_heat);
        auto next_solid_heat=combined_chunk_heat(committed_solid_heat,pending_solid_heat);
        auto next_diffusion=diffusion_progress;const auto& d=result.report.diffusion;
        next_diffusion.substeps+=d.substeps;next_diffusion.rejected_attempts+=d.rejected_attempts;
        next_diffusion.faces+=d.faces;next_diffusion.unresolved_updates+=d.unresolved_updates;
        next_diffusion.exchanged_mass_kg+=d.exchanged_mass_kg;next_diffusion.elapsed_ms+=d.elapsed_ms;
        next_diffusion.merge_out_of_range(d);
        next_diffusion.species_mass_residual=d.species_mass_residual;next_diffusion.species_mass_bound=d.species_mass_bound;
        next_diffusion.energy_residual=d.energy_residual;next_diffusion.energy_bound=d.energy_bound;
        world=std::move(owner); publication=std::move(cells); status=next;
        completed_audit=std::move(next_audit);completed_snapshot.clear();
        committed_chunk_heat=std::move(next_chunk_heat);pending_chunk_heat={};
        committed_solid_heat=std::move(next_solid_heat);pending_solid_heat={};solid_heat_contacts.clear();
        completed_inputs.clear();pending_inputs.clear();pending_input_flags=0;
        thermal_redistribution=std::move(next_redistribution); thermal_entropy=std::move(next_entropy);
        subcell_void_events=next_subcell_events;subcell_void_entropy=std::move(next_subcell_entropy);
        viscous_dissipation=std::move(next_viscous_dissipation);viscous_stored_heat=std::move(next_viscous_stored_heat);
        viscous_entropy=std::move(next_viscous_entropy);viscous_wall_x=std::move(next_viscous_wall_x);viscous_wall_y=std::move(next_viscous_wall_y);
        committed_potential=std::move(next_potential); pending_potential={};
        body_impulse=std::move(next_impulse); gravity_internal_work=std::move(next_gravity_work);
        maximum_energy_residual=std::max(maximum_energy_residual,static_cast<double>(energy_residual));
        thermal_equilibrium_intervals=next_thermal_equilibrium;
        thermal_stages+=result.report.conduction_stages; thermal_contacts+=result.report.conduction_contacts;
        thermal_iterations+=result.report.conduction_iterations; unresolved_thermal_updates+=result.report.unresolved_conduction_updates;
        diffusion_progress=std::move(next_diffusion);
        viscous_stages+=result.report.viscosity_stages;viscous_modes+=result.report.viscosity_modes;
        unresolved_viscous_heat+=result.report.unresolved_viscous_heat_updates;unresolved_viscous_momentum+=result.report.unresolved_viscous_momentum_updates;
        actors=std::move(next_actors); actor_steps=next_steps; consumer_samples=next_consumed; emitter_samples=next_emitted;
        receipt_sequence=work.sequence;
        committed_heat=std::move(next_heat); committed_heat_batches=accepted_heat_batches;
        receipts=std::move(next_receipts); committed_mass=std::move(next_mass);
        committed_material=std::move(next_material); committed_kinetic=std::move(next_kinetic);
        committed_transfer_batches=accepted_transfer_batches;
        pending_transfer_batches=0; pending_receipts.clear(); pending_mass={}; pending_material={}; pending_kinetic={};
        pending_world.reset(); pending_heat_batches=pending_heat_edits=0; pending_heat={};
    } catch (const std::exception& error) { fault(error.what()); }
      catch (...) { fault("Unknown custom native worker failure"); }
}
void queue_domain_heat(const MAOniDomainHeat* requests,std::uint32_t count,MAOniDomainHeatStatus& output) {
    std::lock_guard frame_lock(frame_mutex); std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state != 1) throw std::logic_error("Native domain is not accepting heat");
    if (!requests || count == 0 || count > 256 || pending_heat_edits > 4096-count || pending_heat_batches >= 1024)
        throw std::invalid_argument("Native heat batch or pending capacity is invalid");
    const ArithmeticScope arithmetic;
    MacEnergySum delta;
    auto candidate=prepare_heat(pending_world ? *pending_world : *world,geometry,status.session,status.completed_frames,std::span(requests,count),delta);
    auto next_heat=combined_heat(pending_heat,delta);
    // Scalar status fields are read-only projections. The accumulated account
    // retains its full additive expansion, including cancellation remainders.
    (void)next_heat.value().require_representable();
    (void)combined_heat(committed_heat,next_heat).value().require_representable();
    if (accepted_heat_batches == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native heat account overflow");
    frame_inputs::Batch batch;batch.heat.assign(requests,requests+count);
    auto next_inputs=frame_inputs::appended(pending_inputs,std::move(batch));
    pending_world=std::move(candidate); pending_heat=std::move(next_heat);
    pending_inputs=std::move(next_inputs);
    pending_heat_edits+=count; ++pending_heat_batches; ++accepted_heat_batches;
    output=heat_status();
}
void copy_domain_heat(MAOniDomainHeatStatus& output) {
    std::lock_guard lock(state_mutex); const ArithmeticScope arithmetic; output=heat_status();
}
static DomainChunkHeat exchange_domain_native_heat(int native_cell,float temperature,double capacity_kj,double conductivity,
    float transfer_scale,float contact_scale,float seconds,bool solid,double fluid_conductivity_scale,
    std::uint32_t solid_public_cell=0,float native_old_temperature=0) noexcept {
    DomainChunkHeat output{false,temperature,0};
    if (!suppress_domain_cell(MA_ONI_CHUNK_HEAT,native_cell)) return output;
    output.handled=true;
    std::lock_guard lock(state_mutex);
    if (status.state!=1) return output;
    try {
        const ArithmeticScope arithmetic;
        for (auto value:std::array<double,5>{conductivity,transfer_scale,contact_scale,seconds,fluid_conductivity_scale})
            if (!std::isfinite(value) || value<0) throw std::invalid_argument("Invalid native chunk heat coefficient or interval");
        if (seconds==0) return output;
        const auto pitch=geometry.native_grid_width+2;
        const auto public_cell=static_cast<std::uint32_t>(native_cell)%pitch-1+
            (static_cast<std::uint32_t>(native_cell)/pitch-1)*geometry.native_grid_width;
        const auto cell=domain_index(public_cell);
        const auto& source=pending_world ? *pending_world : *world;
        const auto& mixture=source.state().cells[cell].mixture;
        // A vacuum cell (void phase) exchanges no heat with a body.
        if (!(mixture.mass()>0)) return output;
        const auto reading=source.thermodynamics().evaluate(mixture,mac_material_volume(source.state(),cell));
        const auto& materials=source.thermodynamics().reference().materials();double numerator=0,volume=0;
        for (std::size_t k=0;k<materials.size();++k) {
            const auto amount=reading.partial_volumes_m3[k];
            const auto fluid_k=source.registry()[source.registry().index(materials[k].id)].conductivity;
            if (!std::isfinite(fluid_k) || fluid_k<0) throw std::invalid_argument("Invalid owned fluid conductivity");
            numerator+=amount*fluid_k;volume+=amount;
        }
        const double fluid_k=(numerator/volume)*fluid_conductivity_scale,body_k=conductivity;
        const double small=std::min(fluid_k,body_k),large=std::max(fluid_k,body_k);
        const double conductance=large>0 ? (2*small/(1+small/large))*transfer_scale*contact_scale : 0;
        auto candidate=exchange_mac_native_heat(source,cell,{temperature,capacity_kj},conductance,seconds);
        auto& staged_progress=solid ? pending_solid_heat : pending_chunk_heat;
        auto& completed_progress=solid ? committed_solid_heat : committed_chunk_heat;
        auto progress=staged_progress;const auto& report=candidate.report;
        ++progress.contacts;progress.iterations+=report.iterations;
        if (solid) progress.endpoint={64,1,status.state,solid_public_cell,status.session,status.completed_frames,
            status.completed_frames+1,public_cell,0,native_old_temperature,temperature,candidate.body_temperature_k,seconds};
        progress.requested.add(std::abs(report.requested_heat_j));
        progress.maximum_residual=std::max(progress.maximum_residual,std::abs(report.energy_residual_j));
        progress.maximum_bound=std::max(progress.maximum_bound,report.energy_bound_j);
        if (report.unresolved) ++progress.unresolved;
        if (report.body_heat_loss_j==0) {staged_progress=std::move(progress);return output;}
        if (pending_heat_edits>=4096 || pending_heat_batches>=1024 || accepted_heat_batches==UINT64_MAX)
            throw std::length_error("Native chunk heat exceeded its reserved frame input capacity");
        progress.body_loss.add(report.body_heat_loss_j);progress.fluid_heat.add(report.stored_fluid_heat_j);
        progress.entropy.add(report.entropy_lower_bound_j_per_k);++progress.transfers;
        (void)combined_chunk_heat(completed_progress,progress);
        auto next_heat=pending_heat;next_heat.add(report.body_heat_loss_j);
        (void)combined_heat(committed_heat,next_heat).value().require_representable();
        const std::uint32_t source_id=solid ? 0x4d415348U : 0x4d414348U; // MASH solid / MACH chunk
        frame_inputs::Batch batch;batch.heat.push_back({48,1,status.session,status.completed_frames,public_cell,source_id,report.body_heat_loss_j,1e6});
        auto next_inputs=frame_inputs::appended(pending_inputs,std::move(batch));
        auto next_world=std::make_unique<MacClosedWorld>(std::move(candidate.world));
        // No native endpoint has changed yet. The detour writes these two
        // scalar outputs only after every allocation and reservation succeeds.
        output.temperature=candidate.body_temperature_k;
        output.delta_kj=static_cast<float>(-report.body_heat_loss_j/1000);
        pending_world=std::move(next_world);pending_heat=std::move(next_heat);pending_inputs=std::move(next_inputs);
        staged_progress=std::move(progress);++pending_heat_edits;++pending_heat_batches;++accepted_heat_batches;
    } catch (const std::exception& error) {fault(error.what());}
      catch (...) {fault("Unknown native body/fluid heat failure");}
    return output;
}
DomainChunkHeat exchange_domain_chunk_heat(int native_cell,float temperature,float capacity_kj,float conductivity,
    float transfer_scale,float contact_scale,float seconds) noexcept {
    return exchange_domain_native_heat(native_cell,temperature,capacity_kj,conductivity,transfer_scale,contact_scale,seconds,false,1);
}
void copy_domain_chunk_heat_status(MAOniDomainChunkHeatStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;const auto& p=committed_chunk_heat;
    output={sizeof(output),1,status.state,0,status.session,status.completed_frames,p.contacts,p.transfers,p.unresolved,p.iterations,
        p.body_loss.value().require_representable(),p.fluid_heat.value().require_representable(),p.maximum_residual,p.maximum_bound,
        p.entropy.value().require_representable(),p.requested.value().require_representable()};
}
void copy_domain_solid_heat_status(MAOniDomainSolidHeatStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;const auto& p=committed_solid_heat;
    output={sizeof(output),1,status.state,0,status.session,status.completed_frames,p.contacts,p.transfers,p.unresolved,p.iterations,
        p.body_loss.value().require_representable(),p.fluid_heat.value().require_representable(),p.maximum_residual,p.maximum_bound,
        p.entropy.value().require_representable(),p.requested.value().require_representable()};
}
void copy_domain_solid_heat_endpoint(MAOniDomainSolidHeatEndpoint& output) {
    std::lock_guard lock(state_mutex);output=committed_solid_heat.endpoint;
    output.struct_size=sizeof(output);output.abi_version=1;output.state=status.state;
    output.session=status.session;output.completed_frame=status.completed_frames;
}
void copy_domain_solid_cell_state(MAOniDomainSolidCellState& output) {
    std::lock_guard lock(state_mutex);output=completed_solid_cell;
    output.struct_size=sizeof(output);output.abi_version=1;output.state=status.state;
    output.session=status.session;
}
MAOniDomainStatus frame_status();
std::optional<published_frames::Frame> capture_domain_publication(std::uint64_t native_epoch) {
    std::lock_guard lock(state_mutex); const ArithmeticScope arithmetic;
    if (!enabled.load(std::memory_order_acquire) || !world || status.state != 1) return {};
    published_frames::Frame frame; frame.status = frame_status(); frame.cells = publication;
    frame.native_epoch = native_epoch;
    const auto& p = committed_solid_heat;
    frame.solid_heat = {sizeof(frame.solid_heat), 1, status.state, 0, status.session, status.completed_frames,
        p.contacts, p.transfers, p.unresolved, p.iterations, p.body_loss.value().require_representable(),
        p.fluid_heat.value().require_representable(), p.maximum_residual, p.maximum_bound,
        p.entropy.value().require_representable(), p.requested.value().require_representable()};
    frame.solid_endpoint = p.endpoint;
    frame.solid_endpoint.struct_size = sizeof(frame.solid_endpoint); frame.solid_endpoint.abi_version = 1;
    frame.solid_endpoint.state = status.state; frame.solid_endpoint.session = status.session;
    frame.solid_endpoint.completed_frame = status.completed_frames;
    frame.solid_cell = completed_solid_cell;
    if (p.endpoint.contact_frame == 0) {
        frame.solid_cell.struct_size = sizeof(frame.solid_cell); frame.solid_cell.abi_version = 1;
        frame.solid_cell.state = status.state; frame.solid_cell.session = status.session;
        frame.solid_cell.completed_frame = status.completed_frames; frame.solid_cell.native_ticket = status.native_ticket;
    } else if (frame.solid_cell.session != status.session || frame.solid_cell.completed_frame != status.completed_frames ||
        frame.solid_cell.native_ticket != status.native_ticket) throw std::logic_error("Native publication solid observation is stale");
    return frame;
}
void record_domain_solid_cell_state(const MAOniDomainSolidCellState& input) {
    std::lock_guard lock(state_mutex);
    if(input.struct_size!=sizeof(input) || input.abi_version!=1 || input.reserved || input.state!=1 ||
        input.session!=status.session || input.completed_frame!=status.completed_frames || input.native_ticket!=status.native_ticket ||
        input.solid_public_cell!=committed_solid_heat.endpoint.solid_public_cell)
        throw std::logic_error("Native solid cell observation differs from its completed owner frame");
    completed_solid_cell=input;
}
bool domain_solid_heat_registered(int cell) noexcept {
    std::lock_guard lock(state_mutex);return status.state==1 && solid_heat_cells.contains(cell);
}
void fault_domain_solid_heat(const char* error) noexcept {std::lock_guard lock(state_mutex);fault(error);}
void configure_domain_solid_heat(const MAOniDomainSolidHeatCell* inputs,std::uint32_t count,MAOniDomainSolidHeatStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;
    if(!inputs || count==0 || count>128) throw std::invalid_argument("Invalid solid boundary registration count");
    if(status.state!=1 || !world || pending_world || !receipts.empty())
        throw std::logic_error("Solid boundary registration requires a drained completed owner");
    auto candidate=checked_solid_heat(geometry,world->state().grid.walls,std::span(inputs,count),status.session,status.completed_frames);
    solid_heat_cells=std::move(candidate);solid_heat_contacts.clear();
    const auto& p=committed_solid_heat;
    output={sizeof(output),1,status.state,0,status.session,status.completed_frames,p.contacts,p.transfers,p.unresolved,p.iterations,
        p.body_loss.value().require_representable(),p.fluid_heat.value().require_representable(),p.maximum_residual,p.maximum_bound,
        p.entropy.value().require_representable(),p.requested.value().require_representable()};
}
DomainChunkHeat exchange_domain_solid_heat(int fluid_cell,int solid_cell,std::uint16_t element,float mass,float temperature,float seconds,
    std::uint8_t solid_insulation,std::uint8_t fluid_insulation,float native_old_temperature) noexcept {
    DomainChunkHeat result{true,temperature,0};MAOniDomainSolidHeatCell admitted{};
    {
        std::lock_guard lock(state_mutex);
        if(status.state!=1 || seconds==0) return result;
        const auto found=solid_heat_cells.find(solid_cell);if(found==solid_heat_cells.end()) return result;
        admitted=found->second;
        if(element!=admitted.native_element_index || solid_insulation!=admitted.native_insulation ||
            std::bit_cast<std::uint32_t>(mass)!=std::bit_cast<std::uint32_t>(admitted.mass_kg)) {
            fault("Admitted solid boundary material, mass or insulation changed before its native heat contact");return result;
        }
        const auto pitch=static_cast<std::int64_t>(geometry.native_grid_width)+2;
        const auto dx=std::abs(static_cast<std::int64_t>(fluid_cell)%pitch-solid_cell%pitch);
        const auto dy=std::abs(static_cast<std::int64_t>(fluid_cell)/pitch-solid_cell/pitch);
        if(dx+dy!=1) {fault("Native solid heat contact is not a shared cell face");return result;}
        try {if(!solid_heat_contacts.emplace(fluid_cell,solid_cell).second) return result;}
        catch(...) {fault("Cannot reserve native solid heat contact identity");return result;}
    }
    try {
        const ArithmeticScope arithmetic;
        const double capacity=static_cast<double>(admitted.mass_kg)*admitted.specific_heat_kj_per_kg_k;
        const double solid_scale=static_cast<double>(solid_insulation)/255,fluid_scale=static_cast<double>(fluid_insulation)/255;
        const double conductivity=static_cast<double>(admitted.conductivity_w_per_m_k)*solid_scale*solid_scale;
        return exchange_domain_native_heat(fluid_cell,temperature,capacity,conductivity,1,1,seconds,true,fluid_scale*fluid_scale,
            admitted.public_cell,native_old_temperature);
    } catch(const std::exception& error) {std::lock_guard lock(state_mutex);fault(error.what());return result;}
      catch(...) {std::lock_guard lock(state_mutex);fault("Unknown native solid capacity failure");return result;}
}
MAOniDomainTransferStatus transfer_status() {
    MAOniDomainTransferStatus value{}; value.struct_size=sizeof(value); value.abi_version=MA_ONI_DOMAIN_TRANSFER_ABI_VERSION; value.state=status.state;
    value.pending_batches=pending_transfer_batches; value.session=status.session;
    value.accepted_batches=accepted_transfer_batches; value.committed_batches=committed_transfer_batches;
    value.unacknowledged_receipts=receipts.size();
    for (std::size_t i=0;i<ids.size();++i) value.mass_changes_kg[i]=committed_mass[i].value().require_representable();
    value.material_energy_change_j=committed_material.value().require_representable();
    value.kinetic_energy_change_j=committed_kinetic.value().require_representable(); return value;
}
void copy_domain_transfer_status(MAOniDomainTransferStatus& output) {
    std::lock_guard lock(state_mutex); const ArithmeticScope arithmetic; output=transfer_status();
}
void queue_domain_transfers(const MAOniDomainTransfer* inputs,std::uint32_t count,MAOniDomainTransferStatus& output) {
    std::lock_guard frame_lock(frame_mutex); std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state != 1) throw std::logic_error("Native domain is not accepting transfers");
    if (!inputs || !count || count > 256 || receipts.size()+pending_receipts.size()+count > 4096 || pending_transfer_batches >= 1024)
        throw std::invalid_argument("Invalid native transfer batch or receipt capacity");
    const ArithmeticScope arithmetic;
    GasWork work;
    for (std::uint32_t i=0;i<count;++i) prepare_transfer(work,inputs[i],inputs[i].kind);
    work.validate_accounts();
    if (accepted_transfer_batches == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native transfer batch sequence overflow");
    frame_inputs::Batch batch;batch.transfers.assign(inputs,inputs+count);
    auto next_inputs=frame_inputs::appended(pending_inputs,std::move(batch));
    pending_world=std::move(work.owner); pending_mass=std::move(work.mass);
    pending_potential=std::move(work.potential);
    pending_material=std::move(work.material); pending_kinetic=std::move(work.kinetic); pending_receipts=std::move(work.outbox);
    receipt_sequence=work.sequence; ++accepted_transfer_batches; ++pending_transfer_batches;
    pending_inputs=std::move(next_inputs);
    output=transfer_status();
}
MAOniDomainStatus frame_status() {
    auto next=status;
    next.suppressed_pressure=suppressed[MA_ONI_PRESSURE].load(std::memory_order_relaxed);
    next.suppressed_liquid=suppressed[MA_ONI_LIQUID].load(std::memory_order_relaxed)+
        suppressed[MA_ONI_LIQUID_DISPLACEMENT].load(std::memory_order_relaxed);
    next.suppressed_temperature=suppressed[MA_ONI_TEMPERATURE].load(std::memory_order_relaxed);
    next.suppressed_transition=suppressed[MA_ONI_STATE_TRANSITION].load(std::memory_order_relaxed);
    next.suppressed_post_process=suppressed[MA_ONI_POST_PROCESS].load(std::memory_order_relaxed);
    next.suppressed_gas=suppressed[MA_ONI_GAS].load(std::memory_order_relaxed)+suppressed[MA_ONI_GAS_PAIR].load(std::memory_order_relaxed)+
        suppressed[MA_ONI_GAS_DISPLACEMENT].load(std::memory_order_relaxed);
    return next;
}
void copy_domain(MAOniDomainStatus& output,MAOniDomainCell* cells,std::uint32_t capacity) {
    std::lock_guard frame_lock(frame_mutex);
    std::lock_guard lock(state_mutex);
    if (cells && capacity < publication.size()) throw std::length_error("Native domain snapshot capacity is insufficient");
    if (!cells && capacity) throw std::invalid_argument("Null native domain cell output");
    const auto next=frame_status();
    if (cells) std::copy(publication.begin(),publication.end(),cells);
    output=next;
}
void copy_domain_receipts(MAOniDomainStatus& output,MAOniDomainCell* cells,std::uint32_t capacity,
    MAOniDomainReceipt* result,std::uint32_t receipt_capacity,std::uint32_t& count) {
    std::lock_guard frame_lock(frame_mutex); std::lock_guard lock(state_mutex);
    if (!cells || capacity < publication.size() || receipt_capacity < receipts.size()) throw std::length_error("Native physical frame/receipt capacity is insufficient");
    if (!result && !receipts.empty()) throw std::invalid_argument("Native receipt output is null");
    const auto next=frame_status();
    std::copy(publication.begin(),publication.end(),cells);
    if (!receipts.empty()) std::copy(receipts.begin(),receipts.end(),result);
    output=next; count=static_cast<std::uint32_t>(receipts.size());
}
void ack_domain_receipts(std::uint64_t session,std::uint64_t through) {
    std::lock_guard lock(state_mutex);
    if (session != status.session) throw std::logic_error("Native receipt session changed");
    if (through == acknowledged_receipt) return;
    const auto found=std::find_if(receipts.begin(),receipts.end(),[&](const auto& receipt) { return receipt.sequence == through; });
    if (found == receipts.end()) throw std::invalid_argument("Native receipt acknowledgment is not a published sequence");
    receipts.erase(receipts.begin(),found+1); acknowledged_receipt=through;
}
void configure_domain_actor(const MAOniDomainActor& input,MAOniDomainActorStatus& output) {
    configure_domain_actors(&input,1,output);
}
void configure_actors_impl(const MAOniDomainActor* inputs,std::uint32_t count,MAOniDomainActorStatus& output,
    std::span<const actor_checkpoint::Identity> identities) {
    std::lock_guard frame_lock(frame_mutex); std::lock_guard lock(state_mutex);
    const ArithmeticScope arithmetic;
    if (!enabled.load(std::memory_order_acquire) || status.state != 1) throw std::logic_error("Native domain is not accepting actors");
    if (!inputs || !count || count > 256) throw std::invalid_argument("Native actor batch must contain 1..256 commands");
    if (!identities.empty() && identities.size()!=count) throw std::invalid_argument("Native actor identities do not cover the complete batch");
    auto next=actors;auto pending=pending_actor_restore;
    for (std::uint32_t index=0;index<count;++index) {
        const auto& input=inputs[index];
        if (input.struct_size != sizeof(input) || input.abi_version != 1 || input.reserved || input.flags > 1 ||
            (input.kind != 1 && input.kind != 2) || input.native_handle == -1)
            throw std::invalid_argument("Native actor ABI, handle or kind differs");
        if (input.session != status.session || input.expected_frame != status.completed_frames)
            throw std::logic_error("Native actor source session/frame is stale");
        const auto key=std::make_pair(input.kind,input.native_handle);
        const auto identity=identities.empty() ? actor_checkpoint::Identity{} : identities[index];
        if (!identities.empty() && (!identity.owner || !identity.prefab || identity.component>65535))
            throw std::invalid_argument("Persistent actor identity is invalid");
        if (next.contains(key) && next.at(key).identity!=identity)
            throw std::invalid_argument("A live actor cannot change its persistent identity");
        if (input.flags) {
            if (!next.erase(key)) throw std::invalid_argument("Native actor handle is not registered");
        } else {
            if (world->state().grid.walls[domain_index(input.public_cell)])
                throw std::invalid_argument("Native point actor targets a configured solid cell");
            if (input.species >= ids.size() || !admitted_domain_species(geometry,physics_settings,input.species) ||
                !std::isfinite(input.mass_kg) || input.mass_kg < 0 || !std::isfinite(input.interval_s) ||
                !std::isfinite(input.temperature_k) || !std::isfinite(input.maximum_gas_density_kg_m3) ||
                (input.mass_kg > 0 && input.kind == 2 && (input.interval_s <= 0 || input.temperature_k < 1 ||
                    input.maximum_gas_density_kg_m3 <= 0 || static_cast<double>(static_cast<float>(input.mass_kg)) != input.mass_kg)))
                throw std::invalid_argument("Native point actor phase, material, rate or interval is unsupported");
            if (!next.contains(key) && next.size() >= 256) throw std::length_error("Native actor capacity exhausted");
            for (const auto& [other_key,value] : next)
                if (other_key!=key && identity.owner && value.config.kind==input.kind && value.identity==identity)
                    throw std::invalid_argument("Persistent actor identity is already bound to another handle");
            const auto saved=pending.find({input.kind,identity});
            if (saved!=pending.end()) {
                const auto& a=saved->second;
                if (a.cell!=input.public_cell || a.species!=input.species)
                    throw std::invalid_argument("Restored actor material or location differs from its saved object");
                const bool matches=a.mass==input.mass_kg && a.interval==input.interval_s && a.temperature==input.temperature_k &&
                    a.maximum_density==input.maximum_gas_density_kg_m3;
                if (!matches && input.mass_kg==0) continue; // Inactive OnSpawn bootstrap, never admitted as a new actor.
                if (!matches) throw std::invalid_argument("Restored actor configuration differs from its saved object");
                next[key]=restored_actor(a,input.native_handle,status.session,status.completed_frames);pending.erase(saved);
            } else {
                if (!pending.empty() && !next.contains(key)) throw std::invalid_argument("A restored actor has no matching persistent object identity");
                auto& value=next[key];
                const auto& previous=value.config;
                const bool unchanged=identity.owner && previous.public_cell==input.public_cell && previous.species==input.species &&
                    previous.mass_kg==input.mass_kg && previous.interval_s==input.interval_s && previous.temperature_k==input.temperature_k &&
                    previous.maximum_gas_density_kg_m3==input.maximum_gas_density_kg_m3;
                value.config=input;value.identity=identity;
                if (!unchanged) { value.elapsed=0; value.rate_remainder=0; }
            }
        }
    }
    if (accepted_actor_configurations > std::numeric_limits<std::uint64_t>::max()-count) throw std::overflow_error("Native actor configuration overflow");
    actors=std::move(next);pending_actor_restore=std::move(pending);accepted_actor_configurations+=count;pending_input_flags|=1U;output=actor_status();
}
void configure_domain_actors(const MAOniDomainActor* inputs,std::uint32_t count,MAOniDomainActorStatus& output) {
    configure_actors_impl(inputs,count,output,{});
}
void configure_persistent_domain_actors(const MAOniDomainPersistentActor* inputs,std::uint32_t count,MAOniDomainActorStatus& output) {
    if (!inputs || !count || count>256) throw std::invalid_argument("Persistent actor batch must contain 1..256 commands");
    std::vector<MAOniDomainActor> commands;std::vector<actor_checkpoint::Identity> identities;
    commands.reserve(count);identities.reserve(count);
    for (std::uint32_t i=0;i<count;++i) { commands.push_back(inputs[i].actor);identities.push_back({inputs[i].owner,inputs[i].prefab,inputs[i].component}); }
    configure_actors_impl(commands.data(),count,output,identities);
}
std::uint32_t pending_domain_actor_restores() {
    std::lock_guard lock(state_mutex);return static_cast<std::uint32_t>(pending_actor_restore.size());
}
void copy_domain_actor_status(MAOniDomainActorStatus& output) {
    std::lock_guard lock(state_mutex); output=actor_status();
}
void configure_domain_diffusion(std::uint64_t session,std::uint64_t frame,std::uint32_t mode,double multiplier) {
    const MacDiffusionSettings setting{static_cast<MacDiffusionMode>(mode),multiplier};setting.validate();
    std::lock_guard frame_lock(frame_mutex);std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;
    if(!world || status.state!=1 || status.session!=session || status.completed_frames!=frame || pending_world)
        throw std::logic_error("Diffusion configuration requires a drained current completed native frame");
    if(liquid_domain(geometry,physics_settings) || mixed_domain(geometry,physics_settings))
        throw std::invalid_argument("Molecular gas diffusion currently requires a gas-only domain");
    // Construct first: failed validation leaves the complete physical owner intact.
    auto candidate=std::make_unique<MacClosedWorld>(world->with_diffusion(setting));
    // The next journal hashes its newly configured source image. A reader
    // retaining the old settings must rebase to that exact image; no actor
    // configuration changed here. Preserve any independently pending actor flag.
    world=std::move(candidate);completed_snapshot.clear();completed_inputs.clear();completed_audit.reset();
}
void copy_domain_diffusion_status(MAOniDomainDiffusionStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;output={};
    output.struct_size=sizeof(output);output.abi_version=2;output.state=status.state;
    output.session=status.session;output.completed_frame=status.completed_frames;
    if(world) {
        output.mode=static_cast<std::uint32_t>(world->settings().diffusion.mode);output.multiplier=world->settings().diffusion.multiplier;
        const auto& id=world->transport_profile().identity();std::copy_n(id.c_str(),std::min(id.size(),sizeof(output.profile)-1),output.profile);
    }
    output.substeps=diffusion_progress.substeps;output.rejected_attempts=diffusion_progress.rejected_attempts;
    output.faces=diffusion_progress.faces;output.unresolved_updates=diffusion_progress.unresolved_updates;
    output.exchanged_mass_kg=diffusion_progress.exchanged_mass_kg;output.elapsed_ms=diffusion_progress.elapsed_ms;
    for(double v:diffusion_progress.species_mass_residual)output.maximum_mass_residual_kg=std::max(output.maximum_mass_residual_kg,std::abs(v));
    for(double v:diffusion_progress.species_mass_bound)output.maximum_mass_bound_kg=std::max(output.maximum_mass_bound_kg,v);
    output.energy_residual_j=diffusion_progress.energy_residual.project().value;output.energy_bound_j=diffusion_progress.energy_bound.project().value;
    output.out_of_range_evaluations=diffusion_progress.out_of_range_evaluations;
    output.out_of_range_minimum_temperature_k=diffusion_progress.out_of_range_minimum_temperature_k;
    output.out_of_range_maximum_temperature_k=diffusion_progress.out_of_range_maximum_temperature_k;
    output.out_of_range_minimum_pressure_pa=diffusion_progress.out_of_range_minimum_pressure_pa;
    output.out_of_range_maximum_pressure_pa=diffusion_progress.out_of_range_maximum_pressure_pa;
}
void copy_domain_thermal_status(MAOniDomainThermalStatus& output) {
    std::lock_guard lock(state_mutex); const ArithmeticScope arithmetic;
    output={sizeof(MAOniDomainThermalStatus),2,status.state,world && world->settings().thermal_conduction ? 1U : 0U,
        status.session,status.completed_frames,thermal_stages,thermal_contacts,thermal_iterations,unresolved_thermal_updates,
        thermal_redistribution.value().project().value,thermal_entropy.value().project().value,thermal_equilibrium_intervals};
}
void copy_domain_gravity_status(MAOniDomainGravityStatus& output) {
    std::lock_guard lock(state_mutex); const ArithmeticScope arithmetic;
    const auto gravity=world ? world->settings().gravity_m_per_s2 : 0;
    output={sizeof(MAOniDomainGravityStatus),1,status.state,gravity != 0 ? 1U : 0U,status.session,status.completed_frames,
        gravity,initial_potential,world ? potential(*world) : 0,committed_potential.value().require_representable(),
        pending_potential.value().require_representable(),body_impulse.value().require_representable(),
        gravity_internal_work.value().require_representable(),maximum_energy_residual};
}
void copy_domain_subcell_status(MAOniDomainSubcellStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;
    output={sizeof(output),1,status.state,world ? static_cast<std::uint32_t>(world->settings().subcell_void) : 0U,
        status.session,status.completed_frames,subcell_void_events,subcell_void_entropy.value().require_representable()};
}
void copy_domain_viscosity_status(MAOniDomainViscosityStatus& output) {
    std::lock_guard lock(state_mutex);const ArithmeticScope arithmetic;
    const auto settings=world ? world->settings().viscosity : MacViscosity{};
    output={sizeof(MAOniDomainViscosityStatus),1,status.state,settings.shear_pa_s>0 || settings.bulk_pa_s>0 ? 1U : 0U,
        status.session,status.completed_frames,viscous_stages,viscous_modes,unresolved_viscous_heat,unresolved_viscous_momentum,
        settings.shear_pa_s,settings.bulk_pa_s,viscous_dissipation.value().project().value,viscous_stored_heat.value().project().value,
        viscous_entropy.value().project().value,viscous_wall_x.value().project().value,viscous_wall_y.value().project().value};
}
std::vector<std::uint8_t> checkpoint_domain(std::uint64_t session,std::uint64_t frame) {
    std::lock_guard frame_lock(frame_mutex); MAOniDomainConfig configuration{}; MAOniDomainStatus source{};
    return checkpoint_domain_under_fence(session,frame,configuration,source);
}
std::vector<std::uint8_t> checkpoint_domain_under_fence(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig& configuration,MAOniDomainStatus& source) {
    std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state != 1 || session != status.session || frame != status.completed_frames)
        throw std::logic_error("Physical checkpoint source session/frame is unavailable");
    if (pending_world || !actors.empty() || !pending_actor_restore.empty() || !receipts.empty())
        throw std::logic_error("Physical checkpoint requires drained commands, actors and callback receipts");
    const ArithmeticScope arithmetic; auto image=physical_image(*world,geometry,physics_settings); configuration=geometry; source=frame_status(); return image;
}
void domain_checkpoint_source(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig& configuration,MAOniDomainStatus& source,bool include_actors) {
    std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state!=1 || session!=status.session || frame!=status.completed_frames ||
        pending_world || (!include_actors && !actors.empty()) || !pending_actor_restore.empty() || !receipts.empty())
        throw std::logic_error("Paired source requires one drained completed physical frame and fully rebound actors");
    if (include_actors) for (const auto& [key,actor] : actors) { (void)key;(void)saved_actor(actor); }
    configuration=geometry; source=frame_status();
}
MAOniDomainStatus domain_status_under_fence() {
    std::lock_guard lock(state_mutex); return frame_status();
}
std::vector<std::uint8_t> advance_domain_checkpoint(std::span<const std::uint8_t> image,double seconds) {
    const ArithmeticScope arithmetic;
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Invalid physical checkpoint interval");
    if (configured_domain::has_magic(image)) {
        // The wrapper is self-describing; the restored owner rechecks its
        // admission, solver walls/settings and phase content before advancing.
        auto saved=configured_domain::decode(image);
        validate_configured_layout(saved.geometry,saved.physics,saved.mask);
        const auto registry=domain_registry(0);
        const auto time=MacClosedWorld::from_checkpoint(registry,model(registry,0),saved.physical).time_s();
        const auto owner=restored_configured_owner(saved.geometry,saved.physics,saved.mask,image,time);
        saved.physical=seconds == 0 ? owner->checkpoint() : advance_owned_world(*owner,seconds).world.checkpoint();
        return configured_domain::encode(saved);
    }
    auto registry=SpeciesRegistry::standard();
    auto restored=[&] {
        try { return MacClosedWorld::from_checkpoint(registry,model(registry),image); }
        catch (const std::invalid_argument&) {
            auto shifted=domain_registry(3);
            return MacClosedWorld::from_checkpoint(shifted,model(shifted,3),image);
        }
    }();
    return seconds == 0 ? restored.checkpoint() : advance_owned_world(restored,seconds).world.checkpoint();
}
std::vector<std::uint8_t> copy_completed_domain_snapshot(std::uint64_t session,std::uint64_t frame) {
    std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state!=1 || session!=status.session || frame!=status.completed_frames)
        throw std::logic_error("Completed snapshot requires the current active session/frame");
    if (completed_snapshot.empty()) {
        if (!completed_audit || !completed_audit->completed) throw std::logic_error("Complete frame image is unavailable for non-durable actors");
        const ArithmeticScope arithmetic;
        completed_snapshot=audit_image(*completed_audit->completed,completed_audit->journal.geometry,completed_audit->physics,
            completed_audit->completed_actors);
        if (audit_full_serializations<std::numeric_limits<std::uint64_t>::max()) ++audit_full_serializations;
    }
    return completed_snapshot;
}
std::vector<std::uint8_t> snapshot_domain(std::uint64_t session,std::uint64_t frame) {
    std::lock_guard frame_lock(frame_mutex);MAOniDomainConfig configuration{};MAOniDomainStatus source{};
    return snapshot_domain_under_fence(session,frame,configuration,source);
}
std::vector<std::uint8_t> snapshot_domain_under_fence(std::uint64_t session,std::uint64_t frame,MAOniDomainConfig& configuration,MAOniDomainStatus& source) {
    std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state!=1 || session!=status.session || frame!=status.completed_frames ||
        pending_world || !receipts.empty()) throw std::logic_error("Complete snapshot requires one drained completed frame");
    const ArithmeticScope arithmetic;configuration=geometry;source=frame_status();
    auto physical=physical_image(*world,geometry,physics_settings);
    if (actors.empty() && pending_actor_restore.empty()) return physical;
    actor_checkpoint::Image image{geometry,{},std::move(physical)};
    for (const auto& [key,actor] : actors) { (void)key;image.actors.push_back(saved_actor(actor)); }
    for (const auto& [key,actor] : pending_actor_restore) { (void)key;image.actors.push_back(actor); }
    return actor_checkpoint::encode(image);
}
std::vector<std::uint8_t> advance_domain_snapshot(std::span<const std::uint8_t> image,double seconds) {
    if (!actor_checkpoint::has_magic(image)) return advance_domain_checkpoint(image,seconds);
    const ArithmeticScope arithmetic;
    if (!std::isfinite(seconds) || seconds<0 || seconds>1) throw std::invalid_argument("Invalid complete snapshot interval");
    auto saved=actor_checkpoint::decode(image);
    MAOniDomainPhysics physics{};std::unique_ptr<MacClosedWorld> owner;
    if (configured_scenario(saved.geometry.scenario)) {
        // The enclosed wrapper supplies physics and mask; restoration checks it
        // against the actor header's geometry and every actor's cell/phase.
        const auto layout=configured_domain::decode(saved.physical);physics=layout.physics;
        validate_configured_layout(saved.geometry,physics,layout.mask);
        const auto registry=domain_registry(0);
        const auto time=MacClosedWorld::from_checkpoint(registry,model(registry,0),layout.physical).time_s();
        owner=restored_configured_owner(saved.geometry,physics,layout.mask,image,time);
    } else {
        validate_domain_geometry(saved.geometry);
        // Decode with the same material model as the live owner, without reading
        // or modifying any live session, pending command, callback or account.
        auto registry=domain_registry(saved.geometry.scenario);
        auto physical=MacClosedWorld::from_checkpoint(registry,model(registry,saved.geometry.scenario),saved.physical);
        owner=restored_domain_owner(saved.geometry,image,physical.time_s());
    }
    std::map<std::pair<std::uint32_t,std::int32_t>,Actor> restored;
    for (std::size_t i=0;i<saved.actors.size();++i) {
        const auto& a=saved.actors[i];const auto handle=static_cast<std::int32_t>(i+1);
        restored.emplace(std::make_pair(a.kind,handle),restored_actor(a,handle,1,0));
    }
    if (seconds>0) {
        GasWork work(*owner,saved.geometry,physics);std::uint64_t steps=0,consumed=0,emitted=0;
        prepare_actors(work,restored,seconds,steps,consumed,emitted);
        auto advanced=advance_owned_world(*work.owner,seconds);owner=std::make_unique<MacClosedWorld>(std::move(advanced.world));
    }
    saved.actors.clear();for (const auto& [key,actor] : restored) { (void)key;saved.actors.push_back(saved_actor(actor)); }
    saved.physical=physical_image(*owner,saved.geometry,physics);return actor_checkpoint::encode(saved);
}

std::vector<std::uint8_t> copy_domain_frame_inputs(std::uint64_t session,std::uint64_t frame) {
    std::lock_guard frame_lock(frame_mutex);std::lock_guard lock(state_mutex);
    if (!enabled.load(std::memory_order_acquire) || status.state!=1 || session!=status.session ||
        frame!=status.completed_frames || !completed_audit) throw std::logic_error("Completed frame inputs are unavailable for this session/frame");
    if (completed_inputs.empty()) {
        const ArithmeticScope arithmetic;
        const auto& audit=*completed_audit;
        const auto source=audit_image(*audit.source,audit.journal.geometry,audit.physics,audit.source_actors);
        auto journal=audit.journal;journal.source_sha256=hash_memory(source.data(),source.size());
        completed_inputs=frame_inputs::encode(journal);
        if (audit_full_serializations<std::numeric_limits<std::uint64_t>::max()) ++audit_full_serializations;
        if (audit_journal_serializations<std::numeric_limits<std::uint64_t>::max()) ++audit_journal_serializations;
    }
    return completed_inputs;
}
DomainAuditStatistics domain_audit_statistics() {
    std::lock_guard lock(state_mutex);
    return {completed_audit ? (completed_audit->completed ? 2U : 1U) : 0U,completed_snapshot.size(),completed_inputs.size(),
        audit_full_serializations,audit_journal_serializations};
}
std::vector<std::uint8_t> advance_domain_snapshot_with_inputs(std::span<const std::uint8_t> source,std::span<const std::uint8_t> inputs) {
    const ArithmeticScope arithmetic;const auto journal=frame_inputs::decode(inputs);
    const bool configured=configured_scenario(journal.geometry.scenario);
    if (!configured) validate_domain_geometry(journal.geometry);
    frame_inputs::require((journal.flags&3U)==0,"Frame input replay does not support actor reconfiguration or non-durable actors");
    frame_inputs::require(hash_memory(source.data(),source.size())==journal.source_sha256,"Frame inputs belong to another source snapshot");
    const bool durable=actor_checkpoint::has_magic(source);
    actor_checkpoint::Image saved;
    if (durable) saved=actor_checkpoint::decode(source);
    MAOniDomainPhysics physics{};std::unique_ptr<MacClosedWorld> owner;
    if (configured) {
        // The digest binds the journal to its complete source; the source
        // wrapper supplies physics and mask and must repeat the journal geometry.
        const auto layout=configured_domain::decode(durable ? std::span<const std::uint8_t>(saved.physical) : source);
        physics=layout.physics;
        owner=restored_configured_owner(journal.geometry,physics,layout.mask,source,journal.source_time);
    } else owner=restored_domain_owner(journal.geometry,source,journal.source_time);
    std::map<std::pair<std::uint32_t,std::int32_t>,Actor> restored;
    if (durable) {
        for (std::size_t i=0;i<saved.actors.size();++i) {
            const auto& a=saved.actors[i];const auto handle=static_cast<std::int32_t>(i+1);
            restored.emplace(std::make_pair(a.kind,handle),restored_actor(a,handle,1,0));
        }
    }
    GasWork work(*owner,journal.geometry,physics);
    for (const auto& batch:journal.batches) {
        if (!batch.heat.empty()) {
            auto heat=batch.heat;for (auto& h:heat) {h.session=work.session;h.expected_frame=work.frame;}
            MacEnergySum delta;work.owner=prepare_heat(*work.owner,journal.geometry,work.session,work.frame,heat,delta);
            (void)delta.value().require_representable();
        } else {
            for (auto input:batch.transfers) {
                input.session=work.session;input.expected_frame=work.frame;prepare_transfer(work,input,input.kind);
            }
            work.validate_accounts();
        }
    }
    std::uint64_t steps=0,consumed=0,emitted=0;prepare_actors(work,restored,journal.seconds,steps,consumed,emitted);
    // Old journals retain their saved selection. New live frames explicitly
    // record production, including the first frame after loading a reference save.
    const auto policy=(journal.flags&frame_inputs::production_advance) ?
        MacExecutionPolicy::production : MacExecutionPolicy::exact_equilibrium;
    auto result=physical_image(advance_owned_world(*work.owner,journal.seconds,policy).world,journal.geometry,physics);
    if (!durable) return result;
    saved.actors.clear();for (const auto& [key,actor]:restored) {(void)key;saved.actors.push_back(saved_actor(actor));}
    saved.physical=std::move(result);return actor_checkpoint::encode(saved);
}

}
