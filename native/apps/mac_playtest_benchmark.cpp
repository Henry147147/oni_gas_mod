// Benchmark the actual closed MAC owner using the declared test EOS. This is
// not whole-colony/game evidence. Construction and accuracy review are timed
// separately from advancement; every advance retains the original guards.
#include "../tests/mac_fixture.hpp"
#include <chrono>
#include <iomanip>
#include <numeric>
#include <numbers>
#include <string>

namespace {
using namespace ma;
using namespace ma::experimental;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
struct Totals { std::vector<long double> mass; long double energy=0; };
Totals totals(const MacClosedWorld& owner) {
    Totals result{std::vector<long double>(owner.registry().size()),0};
    for (const auto& cell:owner.state().cells) {
        for (std::size_t k=0;k<result.mass.size();++k) result.mass[k]+=cell.mixture.masses[k];
        result.energy+=cell.mixture.energy;
    }
    const auto inertia=mac_face_inertias(owner.registry(),owner.state(),owner.thermodynamics());
    for (int axis=0;axis<2;++axis) {
        const auto& mass=axis==0?inertia.x:inertia.y;
        const auto momentum=axis==0?mac_field_values(owner.state().momentum_x,owner.state().full_momentum_x):
            mac_field_values(owner.state().momentum_y,owner.state().full_momentum_y);
        for (std::size_t k=0;k<mass.size();++k)
            result.energy+=MacEnergy::kinetic(mass[k],momentum[k]).require_representable();
    }
    return result;
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=6 && argc!=7) throw std::invalid_argument("Usage: mac_playtest_benchmark width height samples reference|equilibrium|balanced uniform|wave [--no-reference]");
        const auto width=std::stoull(argv[1]),height=std::stoull(argv[2]),samples=std::stoull(argv[3]);
        const std::string profile=argv[4],scene=argv[5];
        const bool compare=profile!="reference" && argc==6;
        if (argc==7 && std::string(argv[6])!="--no-reference") throw std::invalid_argument("Unknown benchmark option");
        if (!width || !height || width>100000 || height>100000 || width>100000/height || !samples || samples>101 ||
            (profile!="reference" && profile!="equilibrium" && profile!="balanced") || (scene!="uniform" && scene!="wave"))
            throw std::invalid_argument("Invalid benchmark inputs (at most 100000 cells and 101 samples)");
        const auto construction=Clock::now(); const auto& setup=ma::testing::setup();
        auto state=setup.state(static_cast<std::size_t>(width),static_cast<std::size_t>(height),true);
        if (scene=="wave") for (std::size_t i=0;i<state.cells.size();++i) {
            const double x=(static_cast<double>(i%width)+.5)/static_cast<double>(width);
            state.cells[i].mixture=setup.mixture(1,100000+20*std::sin(2*std::numbers::pi*x),300);
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
        std::vector<double> times; double mass_drift=0,energy_drift=0;
        std::size_t substeps=0,retries=0,iterations=0,skipped=0;
        std::optional<MacClosedWorld> endpoint;
        for (std::size_t n=0;n<samples;++n) {
            const auto start=Clock::now(); auto result=source.advanced(.1,4096,policy); times.push_back(elapsed(start));
            const auto final=totals(result.world);
            for (std::size_t k=0;k<initial.mass.size();++k) if (initial.mass[k]!=0)
                mass_drift=std::max(mass_drift,static_cast<double>(std::abs(final.mass[k]-initial.mass[k])/std::abs(initial.mass[k])));
            energy_drift=std::max(energy_drift,static_cast<double>(std::abs(final.energy-initial.energy)/std::abs(initial.energy)));
            substeps+=result.report.substeps; retries+=result.report.rejected_attempts;
            iterations+=result.report.pressure_iterations; skipped+=result.report.exact_equilibrium_intervals;
            endpoint=std::move(result.world);
        }
        double density_error=0,pressure_error=0,temperature_error=0; bool reference_equal=true;
        if (compare) {
            const MacClosedWorld reference(setup.registry,setup.eos,state);
            const auto accurate=reference.advanced(.1);
            reference_equal=accurate.world.checkpoint()==endpoint->checkpoint();
            for (std::size_t i=0;i<state.cells.size();++i) {
                const auto& a=accurate.world.state().cells[i].mixture; const auto& b=endpoint->state().cells[i].mixture;
                const auto ra=setup.eos->evaluate(a,1),rb=setup.eos->evaluate(b,1);
                density_error=std::max(density_error,std::abs(b.mass()-a.mass())/a.mass());
                pressure_error=std::max(pressure_error,std::abs(rb.mechanical_pressure_pa-ra.mechanical_pressure_pa)/ra.mechanical_pressure_pa);
                temperature_error=std::max(temperature_error,std::abs(rb.temperature_k-ra.temperature_k));
            }
        }
        const double cold=times.front(); auto sorted=times; std::sort(sorted.begin(),sorted.end());
        const auto percentile=[&](double p) { return sorted[static_cast<std::size_t>(std::ceil(p*static_cast<double>(sorted.size())))-1]; };
        std::cout<<std::setprecision(17)<<"{\"schema\":1,\"cells\":"<<width*height<<",\"samples\":"<<samples
            <<",\"simulated_seconds_per_sample\":0.1,\"profile\":\""<<profile<<"\",\"scene\":\""<<scene
            <<"\",\"setup_ms\":"<<setup_ms<<",\"cold_ms\":"<<cold<<",\"p50_ms\":"<<percentile(.5)<<",\"p95_ms\":"<<percentile(.95)
            <<",\"maximum_ms\":"<<sorted.back()<<",\"substeps\":"<<substeps<<",\"retries\":"<<retries
            <<",\"pressure_iterations\":"<<iterations<<",\"exact_equilibrium_intervals\":"<<skipped
            <<",\"relative_mass_drift_including_trace\":"<<mass_drift<<",\"relative_energy_drift\":"<<energy_drift
            <<",\"reference_comparison_performed\":"<<(compare?"true":"false")
            <<",\"reference_checkpoint_equal\":"<<(compare?(reference_equal?"true":"false"):"null");
        if (compare) std::cout<<",\"maximum_relative_density_difference\":"<<density_error
            <<",\"maximum_relative_pressure_difference\":"<<pressure_error<<",\"maximum_temperature_difference_k\":"<<temperature_error;
        else std::cout<<",\"maximum_relative_density_difference\":null,\"maximum_relative_pressure_difference\":null,\"maximum_temperature_difference_k\":null";
        std::cout<<",\"whole_colony_verified\":false,\"100k_25ms_this_case_this_host\":"<<((width*height==100000 && sorted.back()<=25)?"true":"false")<<",\"wall_ms\":[";
        for (std::size_t i=0;i<times.size();++i) std::cout<<(i?",":"")<<times[i];
        std::cout<<"]}\n";
        if (mass_drift>1e-8 || energy_drift>1e-6) throw std::runtime_error("Benchmark conservation gate failed");
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
