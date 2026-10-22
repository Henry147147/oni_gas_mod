#include "mac_fixture.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <bit>
#include <fstream>
#include <iterator>

namespace {
using namespace ma; using namespace ma::experimental;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::uint64_t hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t result=UINT64_C(14695981039346656037);
    for (auto byte : bytes) result=(result^byte)*UINT64_C(1099511628211);
    return result;
}
void put(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint64_t value,std::size_t count=8) {
    for (std::size_t i=0;i<count;++i) bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));
}
void seal(std::vector<std::uint8_t>& bytes) { put(bytes,bytes.size()-8,hash(std::span(bytes).first(bytes.size()-8))); }
MacClosedWorld owner(const ma::testing::Setup& setup,bool wet,
    MacShockHeatRegularization heat=MacShockHeatRegularization::compression_activated,
    MacCompressionViscosity compression=MacCompressionViscosity::acoustic_quadratic) {
    auto state=setup.state(4,3,false,wet,true);
    const auto inertia=mac_face_inertias(setup.registry,state,*setup.eos);
    state.momentum_x[state.grid.x_face(2,0)]=inertia.x[state.grid.x_face(2,0)]*.1;
    state.momentum_y[state.grid.y_face(3,1)]=inertia.y[state.grid.y_face(3,1)]*-.025;
    MacClosedSettings settings; settings.gravity_m_per_s2=9.81; settings.thermal_conduction=!wet;
    settings.shock_heat=heat; settings.compression_viscosity=compression;
    return MacClosedWorld(setup.registry,setup.eos,std::move(state),settings).advanced(.02).world;
}
void game_profile_checkpoint(const ma::testing::Setup& setup) {
    MacClosedSettings settings;
    settings.numerical_kernel=MacNumericalKernel::production;
    settings.vacuum=MacVacuumRepresentation::void_phase;
    settings.step_control=MacStepControl::regional_predictive;
    const auto state=setup.state(4,3,false);
    const MacClosedWorld legacy(setup.registry,setup.eos,state,settings);
    const auto old_image=legacy.checkpoint();
    require(old_image.at(8)==15,"Legacy regional owner changed its checkpoint version");
    for (const double threshold : {0.,std::numeric_limits<double>::denorm_min(),1e-6}) {
        settings.game_profile=MacGameProfile::base_game_floor;
        settings.trace_sink_threshold_kg=threshold;
        const MacClosedWorld source(setup.registry,setup.eos,state,settings);
        const auto image=source.checkpoint();
        const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
        require(image.at(8)==16 && restored.checkpoint()==image &&
            restored.settings().game_profile==MacGameProfile::base_game_floor &&
            restored.settings().trace_sink_threshold_kg==threshold,
            "Saved game profile or trace threshold changed on restore");
        require(source.advanced(.02).world.checkpoint()==restored.advanced(.02).world.checkpoint(),
            "Saved game profile continuation changed checkpoint bytes");
        std::uint32_t identity_bytes=0;
        for (std::size_t i=0;i<4;++i) identity_bytes|=static_cast<std::uint32_t>(image[12+i])<<(8*i);
        const std::size_t profile=16+identity_bytes+16+26+45+16+4+12+4+4+4+4+4+8;
        require(image.at(profile)==1,"Saved game profile has the wrong wire position");
        for (int malformed=0;malformed<4;++malformed) {
            auto corrupt=image;
            if (malformed<2) put(corrupt,profile,malformed==0 ? 0 : 2,4);
            else put(corrupt,profile+4,std::bit_cast<std::uint64_t>(malformed==2 ? -1. : 1e-5));
            seal(corrupt); bool rejected=false;
            try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); }
            catch (const std::exception&) { rejected=true; }
            require(rejected,"Malformed saved game profile or trace threshold was accepted");
        }
    }
    const auto old_restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,old_image);
    require(old_restored.settings().game_profile==MacGameProfile::legacy && old_restored.settings().trace_sink_threshold_kg==0 &&
        old_restored.checkpoint()==old_image && legacy.checkpoint()==old_image,
        "An old save acquired a game profile or changed bytes");
    const MacClosedWorld version16(setup.registry,setup.eos,state,settings);
    const auto version16_image=version16.checkpoint();
    for (unsigned policies=1;policies<4;++policies) {
        settings.compatible_void_pressure_work=(policies&1)!=0;
        settings.recover_mixed_eos_endpoint=(policies&2)!=0;
        const MacClosedWorld source(setup.registry,setup.eos,state,settings);
        const auto image=source.checkpoint();
        const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
        require(image.at(8)==17 && restored.checkpoint()==image &&
            restored.settings().compatible_void_pressure_work==settings.compatible_void_pressure_work &&
            restored.settings().recover_mixed_eos_endpoint==settings.recover_mixed_eos_endpoint &&
            source.advanced(.02).world.checkpoint()==restored.advanced(.02).world.checkpoint(),
            "Saved game root/work policies changed on restore or continuation");
        std::uint32_t identity_bytes=0;
        for (std::size_t i=0;i<4;++i) identity_bytes|=static_cast<std::uint32_t>(image[12+i])<<(8*i);
        const std::size_t flags=16+identity_bytes+16+26+45+16+4+12+4+4+4+4+4+8+12;
        require(image.at(flags)==(policies&1) && image.at(flags+1)==((policies>>1)&1),
            "Saved game root/work policy wire position changed");
        for (unsigned malformed=0;malformed<4;++malformed) {
            auto corrupt=image;
            if (malformed<2) corrupt.at(flags+malformed)=2;
            else if (malformed==2) { corrupt.at(flags)=0; corrupt.at(flags+1)=0; }
            else put(corrupt,8,16,4);
            seal(corrupt); bool refused=false;
            try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); }
            catch (const std::exception&) { refused=true; }
            require(refused,"Malformed or noncanonical game root/work policies were accepted");
        }
    }
    const auto restored16=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,version16_image);
    require(!restored16.settings().compatible_void_pressure_work && !restored16.settings().recover_mixed_eos_endpoint &&
        restored16.checkpoint()==version16_image &&
        version16.advanced(.02).world.checkpoint()==restored16.advanced(.02).world.checkpoint(),
        "Version16 acquired new game root/work policies or changed continuation");
    settings.compatible_void_pressure_work=false; settings.recover_mixed_eos_endpoint=false;
    for (const double invalid : {-1.,std::nextafter(1e-6,1.),std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        settings.trace_sink_threshold_kg=invalid; bool rejected=false;
        try { settings.validate(); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Invalid trace-sink threshold was accepted");
    }
    settings.game_profile=MacGameProfile::legacy; settings.trace_sink_threshold_kg=1e-9;
    bool rejected=false;
    try { settings.validate(); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Legacy profile enabled a trace sink");
    settings.trace_sink_threshold_kg=0;
    for (unsigned policy=0;policy<2;++policy) {
        settings.compatible_void_pressure_work=policy==0; settings.recover_mixed_eos_endpoint=policy==1;
        rejected=false;
        try { settings.validate(); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected,"Legacy profile enabled a new game root/work policy");
    }
    std::cout << "PASS saved game root/work policies, old continuation and malformed settings\n";
}
}
int main(int argc,char** argv) {
    try {
        const auto& setup=ma::testing::setup();
        game_profile_checkpoint(setup);
        for (const bool wet : {false,true}) for (const auto heat : {MacShockHeatRegularization::none,MacShockHeatRegularization::compression_activated,MacShockHeatRegularization::pressure_sensed_acoustic,MacShockHeatRegularization::pressure_sensed_series})
            for (const auto compression : {MacCompressionViscosity::none,MacCompressionViscosity::quadratic,MacCompressionViscosity::acoustic_quadratic}) {
            const auto source=owner(setup,wet,heat,compression); const auto image=source.checkpoint(); const auto fingerprint=ma::testing::fingerprint(source.state());
            const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
            require(restored.checkpoint()==image && ma::testing::fingerprint(restored.state())==fingerprint &&
                restored.time_s()==source.time_s() && restored.revision()==source.revision() && restored.settings().shock_heat==heat &&
                restored.settings().compression_viscosity==compression,
                "Physical checkpoint changed canonical state, trace/passive quantities, heat setting or clock");
            const auto expected=source.advanced(.02),resumed=restored.advanced(.02);
            require(expected.world.checkpoint()==resumed.world.checkpoint() && expected.report.substeps==resumed.report.substeps &&
                expected.report.shock_heat_stages==resumed.report.shock_heat_stages &&
                expected.report.absolute_shock_heat_transfer_wide_j.mantissa()==resumed.report.absolute_shock_heat_transfer_wide_j.mantissa() &&
                expected.report.absolute_shock_heat_transfer_wide_j.exponent()==resumed.report.absolute_shock_heat_transfer_wide_j.exponent() &&
                expected.report.compression_events==resumed.report.compression_events &&
                expected.report.compression_heat_wide_j.mantissa()==resumed.report.compression_heat_wide_j.mantissa() &&
                expected.report.compression_heat_wide_j.exponent()==resumed.report.compression_heat_wide_j.exponent() &&
                (expected.report.shock_heat_stages>0)==(heat!=MacShockHeatRegularization::none),
                "Checkpoint continuation differs from the uninterrupted physical owner");
            auto corrupt=image; corrupt[image.size()/2]^=1; bool rejected=false;
            try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); } catch (const std::exception&) { rejected=true; }
            require(rejected,"Damaged physical checkpoint passed its integrity check");
            // Recompute the checksum so malformed structural/clock inputs reach
            // their semantic validators instead of only testing the checksum.
            std::uint32_t identity_bytes=0; for (std::size_t i=0;i<4;++i) identity_bytes|=static_cast<std::uint32_t>(image[12+i])<<(8*i);
            const auto clock=16+identity_bytes;
            // Version 2 appends one u32 heat enum after the conduction flag.
            const auto shock_setting=clock+16+26+41;
            require(image.at(shock_setting)==static_cast<std::uint8_t>(heat),"Checkpoint heat setting has the wrong wire position");
            for (int malformed=0;malformed<9;++malformed) {
                corrupt=image;
                if (malformed==0) put(corrupt,clock,std::bit_cast<std::uint64_t>(-1.0));
                if (malformed==1) put(corrupt,clock+8,0);
                if (malformed==2) put(corrupt,clock+16,UINT32_MAX,4);
                if (malformed==3) corrupt.insert(corrupt.end()-8,1);
                if (malformed==4) put(corrupt,8,1,4);
                if (malformed==5) put(corrupt,8,3,4);
                if (malformed==6) put(corrupt,shock_setting,99,4);
                if (malformed==7) corrupt.at(shock_setting-1)=2;
                if (malformed==8) put(corrupt,shock_setting-9,99,4);
                seal(corrupt); rejected=false;
                try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); } catch (const std::exception&) { rejected=true; }
                require(rejected,"Malformed saved version, heat setting, clock, geometry or trailing data was accepted");
            }
            for (std::size_t size=0;size<image.size();++size) {
                rejected=false;
                try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,std::span(image).first(size)); } catch (const std::exception&) { rejected=true; }
                require(rejected,"A truncated physical checkpoint was accepted");
            }
            auto materials=setup.eos->reference().materials(); materials[0].provenance+=" changed";
            const auto foreign=std::make_shared<const FluidThermodynamics>(setup.registry,setup.eos->reference().profile_version(),std::move(materials));
            rejected=false;
            try { (void)MacClosedWorld::from_checkpoint(setup.registry,foreign,image); } catch (const std::invalid_argument&) { rejected=true; }
            require(rejected && source.checkpoint()==image,"Foreign EOS checkpoint acceptance or failed parsing mutated the physical owner");
        }
        auto range=setup.state(2,2,false);
        auto full=mac_field_values(range.momentum_x,range.full_momentum_x);
        const auto face=range.grid.x_face(1,0);
        full[face]=MacScaled::from_parts(.5,MacExponent::from_decimal("-9223372036854775809"));
        store_mac_field(range.momentum_x,range.full_momentum_x,full);
        const MacClosedWorld tiny(setup.registry,setup.eos,range); const auto tiny_image=tiny.checkpoint();
        const auto tiny_restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,tiny_image);
        require(tiny_restored.checkpoint()==tiny_image && !tiny_restored.state().full_momentum_x[face].is_zero() &&
            tiny_restored.state().momentum_x[face]==0,"Physical checkpoint discarded below-range canonical momentum");
        if (argc==3) {
            const std::string operation=argv[1];
            if (operation=="--write-checkpoint" || operation=="--write-checkpoint-no-heat" || operation=="--write-checkpoint-quadratic") {
                const auto heat=operation=="--write-checkpoint-no-heat" ? MacShockHeatRegularization::none :
                    operation=="--write-checkpoint-quadratic" ? MacShockHeatRegularization::compression_activated : MacShockHeatRegularization::pressure_sensed_series;
                const auto compression=operation=="--write-checkpoint-quadratic" ? MacCompressionViscosity::quadratic : MacCompressionViscosity::acoustic_quadratic;
                const auto image=owner(setup,false,heat,compression).checkpoint(); std::ofstream output(argv[2],std::ios::binary);
                output.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));
                require(output.good(),"Could not write the independent cross-platform checkpoint fixture");
            } else if (operation=="--read-checkpoint") {
                std::ifstream input(argv[2],std::ios::binary); require(input.good(),"Could not read the cross-platform checkpoint fixture");
                const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(input),{}};
                require(MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image).checkpoint()==image,
                    "Cross-platform checkpoint did not retain the complete original bytes");
            } else throw std::invalid_argument("Unknown checkpoint fixture operation");
        } else require(argc==1,"Invalid checkpoint test arguments");
        std::cout<<"PASS physical checkpoint, exact continuation for all compression/heat modes, trace/passive/full momentum, model identity and malformed/truncated images\n";
        std::cout<<"PASS saved game profile and trace threshold, legacy identity, exact continuation and malformed settings\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
