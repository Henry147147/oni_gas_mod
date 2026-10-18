// Benchmark the actual closed MAC owner using the declared test EOS. This is
// not whole-colony/game evidence. Construction and accuracy review are timed
// separately from advancement; every advance retains the original guards.
#include "../tests/mac_fixture.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <chrono>
#if defined(__linux__)
#include <ctime>
#endif
#include <fstream>
#include <iomanip>
#include <numeric>
#include <numbers>
#include <string>

namespace {
using namespace ma;
using namespace ma::experimental;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
// Process CPU time (all threads), where the platform exposes it.
double process_cpu_ms() {
#if defined(__linux__)
    timespec t{}; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t); return static_cast<double>(t.tv_sec)*1e3+static_cast<double>(t.tv_nsec)*1e-6;
#else
    return -1;
#endif
}
struct Totals { std::vector<MacEnergySum> mass; MacEnergySum energy; };
double relative_difference(const MacEnergySum& after,const MacEnergySum& before) {
    MacEnergySum difference;
    for(std::size_t i=0;i<after.size();++i) difference.add(after.component(i));
    for(std::size_t i=0;i<before.size();++i) difference.add(before.component(i).negated());
    if(difference.sign()==0) return 0;
    const auto scale=before.value().magnitude();
    if(scale.is_zero()) throw std::runtime_error("Nonzero benchmark change has zero reference inventory");
    return difference.value().magnitude().shared(MacEnergy::from_double(1),scale).project().value;
}
Totals totals(const MacClosedWorld& owner) {
    Totals result{std::vector<MacEnergySum>(owner.registry().size()),{}};
    for (const auto& cell:owner.state().cells) {
        for (std::size_t k=0;k<result.mass.size();++k) result.mass[k].add(cell.mixture.masses[k]);
        result.energy.add(cell.mixture.energy);
    }
    const auto inertia=mac_face_inertias(owner.registry(),owner.state(),owner.thermodynamics());
    for (int axis=0;axis<2;++axis) {
        const auto& mass=axis==0?inertia.x:inertia.y;
        const auto momentum=axis==0?mac_field_values(owner.state().momentum_x,owner.state().full_momentum_x):
            mac_field_values(owner.state().momentum_y,owner.state().full_momentum_y);
        for (std::size_t k=0;k<mass.size();++k)
            result.energy.add(MacEnergy::kinetic(mass[k],momentum[k]));
    }
    return result;
}
std::size_t active_cells(const MacTransportState& before,const MacTransportState& after) {
    const auto x0=mac_field_values(before.momentum_x,before.full_momentum_x),x1=mac_field_values(after.momentum_x,after.full_momentum_x);
    const auto y0=mac_field_values(before.momentum_y,before.full_momentum_y),y1=mac_field_values(after.momentum_y,after.full_momentum_y);
    const auto same=[](const MacScaled& a,const MacScaled& b) {return a.mantissa()==b.mantissa() && a.exponent()==b.exponent();};
    const auto& g=before.grid;std::size_t count=0;
    for(std::size_t i=0;i<before.cells.size();++i) if(!g.walls[i]) {
        const auto& a=before.cells[i].mixture;const auto& b=after.cells[i].mixture;
        const auto x=i%g.width,y=i/g.width;
        const auto l=g.x_face(x,y),r=g.x_face((x+1)%g.x_width(),y);
        const auto d=g.y_face(x,y),u=g.y_face(x,(y+1)%g.y_height());
        if(a.energy!=b.energy || a.masses!=b.masses || !same(x0[l],x1[l]) || !same(x0[r],x1[r]) ||
            !same(y0[d],y1[d]) || !same(y0[u],y1[u])) ++count;
    }
    return count;
}
}
int main(int argc,char** argv) {
    try {
        if (argc<6) throw std::invalid_argument("Usage: mac_playtest_benchmark width height samples reference|equilibrium|balanced uniform|wave [--no-reference] [--continuation] [--checkpoint-output path] [--kernel reference|production] [--threads N]");
        const auto width=std::stoull(argv[1]),height=std::stoull(argv[2]),samples=std::stoull(argv[3]);
        const std::string profile=argv[4],scene=argv[5];
        bool no_reference=false,continuation=false;std::string checkpoint_path,kernel="reference";
        MacKernelOptions kernel_options;bool kernel_given=false,threads_given=false;
        for(int i=6;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--no-reference" && !no_reference) no_reference=true;
            else if(option=="--continuation" && !continuation) continuation=true;
            else if(option=="--checkpoint-output" && checkpoint_path.empty() && i+1<argc) checkpoint_path=argv[++i];
            else if(option=="--kernel" && !kernel_given && i+1<argc) { kernel=argv[++i]; kernel_given=true; }
            else if(option=="--threads" && !threads_given && i+1<argc) { kernel_options.threads=static_cast<unsigned>(std::stoul(argv[++i])); threads_given=true; }
            else throw std::invalid_argument("Unknown, duplicate or incomplete benchmark option");
        }
        if ((kernel!="reference" && kernel!="production") || (kernel=="reference" && threads_given))
            throw std::invalid_argument("Invalid kernel/threads options (threads apply to the production kernel)");
        const bool production=kernel=="production";
        kernel_options.validate();
        // The production kernel is always compared with the reference oracle
        // unless explicitly skipped.
        const bool compare=(profile!="reference" || production) && !no_reference;
        if (width<2 || !height || width>250000 || height>250000 || width>250000/height || !samples || samples>101 ||
            (profile!="reference" && profile!="equilibrium" && profile!="balanced") || (scene!="uniform" && scene!="wave"))
            throw std::invalid_argument("Invalid benchmark inputs (at most 250000 cells and 101 samples)");
        constexpr double wave_amplitude_pa=20;
        const auto construction=Clock::now(); const auto& setup=ma::testing::setup();
        auto state=setup.state(static_cast<std::size_t>(width),static_cast<std::size_t>(height),true);
        if (scene=="wave") for (std::size_t i=0;i<state.cells.size();++i) {
            const double x=(static_cast<double>(i%width)+.5)/static_cast<double>(width);
            state.cells[i].mixture=setup.mixture(1,100000+wave_amplitude_pa*std::sin(2*std::numbers::pi*x),300);
        }
        MacClosedSettings settings;
        if (profile=="balanced") {
            settings.maximum_timestep_s=.1;
            settings.reconstruction=MacMaterialReconstruction::donor_cell;
            settings.momentum_reconstruction=MacMomentumReconstruction::donor_cell;
        }
        const MacClosedWorld source(setup.registry,setup.eos,state,settings);
        const auto initial=totals(source); const double setup_ms=elapsed(construction);
        const auto policy=profile=="reference"?MacExecutionPolicy::reference:MacExecutionPolicy::exact_equilibrium;
        if (production) set_mac_kernel_default_options(kernel_options);
        // Reference profile with the production kernel: every interval runs the
        // kernel (no equilibrium certificate); other profiles use the
        // production policy (certificate first, then the kernel).
        const auto advance=[&](const MacClosedWorld& world) {
            if (!production) return world.advanced(.1,4096,policy);
            if (profile=="reference") return advance_mac_production(world,.1,4096,kernel_options);
            return world.advanced(.1,4096,MacExecutionPolicy::production);
        };
        std::vector<double> times,cpu; double mass_drift=0,energy_drift=0;
        std::size_t substeps=0,retries=0,iterations=0,skipped=0;
        std::optional<MacClosedWorld> endpoint;
        std::size_t minimum_active=static_cast<std::size_t>(width*height);
        for (std::size_t n=0;n<samples;++n) {
            const auto& input=continuation && endpoint ? *endpoint : source;
            const double cpu_start=process_cpu_ms();
            const auto start=Clock::now(); auto result=advance(input); times.push_back(elapsed(start));
            cpu.push_back(cpu_start<0 ? -1 : process_cpu_ms()-cpu_start);
            minimum_active=std::min(minimum_active,active_cells(input.state(),result.world.state()));
            const auto final=totals(result.world);
            for (std::size_t k=0;k<initial.mass.size();++k)
                mass_drift=std::max(mass_drift,relative_difference(final.mass[k],initial.mass[k]));
            energy_drift=std::max(energy_drift,relative_difference(final.energy,initial.energy));
            substeps+=result.report.substeps; retries+=result.report.rejected_attempts;
            iterations+=result.report.pressure_iterations; skipped+=result.report.exact_equilibrium_intervals;
            endpoint=std::move(result.world);
        }
        double density_error=0,pressure_error=0,temperature_error=0,pressure_amplitude_error=0,oxygen_error=0,constituent_error=0;
        bool reference_equal=true;
        if (compare) {
            MacClosedWorld accurate(setup.registry,setup.eos,state);
            for(std::size_t n=0;n<(continuation ? samples : 1);++n) accurate=accurate.advanced(.1).world;
            reference_equal=accurate.checkpoint()==endpoint->checkpoint();
            for (std::size_t i=0;i<state.cells.size();++i) {
                const auto& a=accurate.state().cells[i].mixture; const auto& b=endpoint->state().cells[i].mixture;
                const auto ra=setup.eos->evaluate(a,1),rb=setup.eos->evaluate(b,1);
                density_error=std::max(density_error,std::abs(b.mass()-a.mass())/a.mass());
                pressure_error=std::max(pressure_error,std::abs(rb.mechanical_pressure_pa-ra.mechanical_pressure_pa)/ra.mechanical_pressure_pa);
                temperature_error=std::max(temperature_error,std::abs(rb.temperature_k-ra.temperature_k));
                if(scene=="wave") pressure_amplitude_error=std::max(pressure_amplitude_error,
                    std::abs(rb.mechanical_pressure_pa-ra.mechanical_pressure_pa)/wave_amplitude_pa);
                const auto oxygen=setup.registry.index("oxygen");
                const double pa=ra.gas_pressure_pa*(a.masses[oxygen]/setup.registry[oxygen].molar_mass)/a.gas_moles(setup.registry);
                const double pb=rb.gas_pressure_pa*(b.masses[oxygen]/setup.registry[oxygen].molar_mass)/b.gas_moles(setup.registry);
                oxygen_error=std::max(oxygen_error,std::abs(pb-pa)/pa);
                for(std::size_t k=0;k<a.masses.size();++k) if(a.masses[k]>0)
                    constituent_error=std::max(constituent_error,std::abs(b.masses[k]-a.masses[k])/a.masses[k]);
            }
        }
        double checkpoint_ms=0;
        if(!checkpoint_path.empty()) {
            const auto start=Clock::now();const auto bytes=endpoint->checkpoint();
            std::ofstream output(checkpoint_path,std::ios::binary|std::ios::trunc);
            output.exceptions(std::ios::badbit|std::ios::failbit);
            output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));output.close();
            checkpoint_ms=elapsed(start);
        }
        const double cold=times.front(); auto sorted=times; std::sort(sorted.begin(),sorted.end());
        const auto percentile=[&](double p) { return sorted[static_cast<std::size_t>(std::ceil(p*static_cast<double>(sorted.size())))-1]; };
        const bool active_case=scene=="wave" && minimum_active==width*height;
        std::cout<<std::setprecision(17)<<"{\"schema\":2,\"cells\":"<<width*height<<",\"samples\":"<<samples
            <<",\"simulated_seconds_per_sample\":0.1,\"profile\":\""<<profile<<"\",\"scene\":\""<<scene
            <<"\",\"sampling\":\""<<(continuation?"continuation":"repeated_source")<<"\",\"minimum_active_cells\":"<<minimum_active
            <<",\"wave_amplitude_pa\":"<<(scene=="wave"?wave_amplitude_pa:0)
            <<",\"maximum_timestep_s\":"<<settings.maximum_timestep_s
            <<",\"material_reconstruction\":"<<static_cast<int>(settings.reconstruction)
            <<",\"momentum_reconstruction\":"<<static_cast<int>(settings.momentum_reconstruction)
            <<",\"compression_viscosity\":"<<static_cast<int>(settings.compression_viscosity)
            <<",\"shock_heat\":"<<static_cast<int>(settings.shock_heat)
            <<",\"checkpoint_captured\":"<<(!checkpoint_path.empty()?"true":"false")<<",\"checkpoint_capture_ms\":"<<checkpoint_ms
            <<",\"setup_ms\":"<<setup_ms<<",\"cold_ms\":"<<cold<<",\"p50_ms\":"<<percentile(.5)<<",\"p95_ms\":"<<percentile(.95)
            <<",\"maximum_ms\":"<<sorted.back()<<",\"substeps\":"<<substeps<<",\"retries\":"<<retries
            <<",\"pressure_iterations\":"<<iterations<<",\"exact_equilibrium_intervals\":"<<skipped
            <<",\"relative_mass_drift_including_trace\":"<<mass_drift<<",\"relative_energy_drift\":"<<energy_drift
            <<",\"reference_comparison_performed\":"<<(compare?"true":"false")
            <<",\"reference_checkpoint_equal\":"<<(compare?(reference_equal?"true":"false"):"null");
        if (compare) {
            std::cout<<",\"maximum_relative_density_difference\":"<<density_error
            <<",\"maximum_relative_pressure_difference\":"<<pressure_error<<",\"maximum_temperature_difference_k\":"<<temperature_error
            <<",\"maximum_pressure_difference_over_disturbance\":";
            if(scene=="wave") std::cout<<pressure_amplitude_error;else std::cout<<"null";
            std::cout<<",\"maximum_relative_oxygen_partial_pressure_difference\":"<<oxygen_error
            <<",\"maximum_relative_constituent_difference_including_trace\":"<<constituent_error;
        }
        else std::cout<<",\"maximum_relative_density_difference\":null,\"maximum_relative_pressure_difference\":null,\"maximum_temperature_difference_k\":null,\"maximum_pressure_difference_over_disturbance\":null,\"maximum_relative_oxygen_partial_pressure_difference\":null,\"maximum_relative_constituent_difference_including_trace\":null";
        std::cout<<",\"whole_colony_verified\":false,\"100k_25ms_this_case_this_host\":"<<((active_case && width*height==100000 && sorted.back()<=25)?"true":"false")
            <<",\"250k_62_5ms_solver_only_this_host\":"<<((active_case && continuation && width*height==250000 && percentile(.95)<=62.5)?"true":"false")
            <<",\"wall_ms\":[";
        for (std::size_t i=0;i<times.size();++i) std::cout<<(i?",":"")<<times[i];
        std::cout<<"]";
        // Production-only fields; the reference output keeps its schema.
        if (production) {
            std::cout<<",\"kernel\":\"production\",\"threads\":"<<kernel_options.threads
                <<",\"ns_per_cell_substep_p50\":"<<(percentile(.5)*1e6/(static_cast<double>(width*height)*static_cast<double>(substeps)/static_cast<double>(samples)))
                <<",\"process_cpu_ms\":[";
            for (std::size_t i=0;i<cpu.size();++i) { if(i) std::cout<<","; if(cpu[i]<0) std::cout<<"null"; else std::cout<<cpu[i]; }
            std::cout<<"]";
        }
        std::cout<<"}\n";
        if (mass_drift>1e-8 || energy_drift>1e-6) throw std::runtime_error("Benchmark conservation gate failed");
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
