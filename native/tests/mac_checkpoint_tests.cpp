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
MacClosedWorld owner(const ma::testing::Setup& setup,bool wet) {
    auto state=setup.state(4,3,false,wet,true);
    const auto inertia=mac_face_inertias(setup.registry,state,*setup.eos);
    state.momentum_x[state.grid.x_face(2,0)]=inertia.x[state.grid.x_face(2,0)]*.1;
    state.momentum_y[state.grid.y_face(3,1)]=inertia.y[state.grid.y_face(3,1)]*-.025;
    MacClosedSettings settings; settings.gravity_m_per_s2=9.81; settings.thermal_conduction=!wet;
    return MacClosedWorld(setup.registry,setup.eos,std::move(state),settings).advanced(.02).world;
}
}
int main(int argc,char** argv) {
    try {
        const auto& setup=ma::testing::setup();
        for (const bool wet : {false,true}) {
            const auto source=owner(setup,wet); const auto image=source.checkpoint(); const auto fingerprint=ma::testing::fingerprint(source.state());
            const auto restored=MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image);
            require(restored.checkpoint()==image && ma::testing::fingerprint(restored.state())==fingerprint &&
                restored.time_s()==source.time_s() && restored.revision()==source.revision(),"Physical checkpoint changed canonical state, trace/passive quantities or clock");
            const auto expected=source.advanced(.02),resumed=restored.advanced(.02);
            require(expected.world.checkpoint()==resumed.world.checkpoint() && expected.report.substeps==resumed.report.substeps,
                "Checkpoint continuation differs from the uninterrupted physical owner");
            auto corrupt=image; corrupt[image.size()/2]^=1; bool rejected=false;
            try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); } catch (const std::exception&) { rejected=true; }
            require(rejected,"Damaged physical checkpoint passed its integrity check");
            // Recompute the checksum so malformed structural/clock inputs reach
            // their semantic validators instead of only testing the checksum.
            std::uint32_t identity_bytes=0; for (std::size_t i=0;i<4;++i) identity_bytes|=static_cast<std::uint32_t>(image[12+i])<<(8*i);
            const auto clock=16+identity_bytes;
            for (int malformed=0;malformed<4;++malformed) {
                corrupt=image;
                if (malformed==0) put(corrupt,clock,std::bit_cast<std::uint64_t>(-1.0));
                if (malformed==1) put(corrupt,clock+8,0);
                if (malformed==2) put(corrupt,clock+16,UINT32_MAX,4);
                if (malformed==3) corrupt.insert(corrupt.end()-8,1);
                seal(corrupt); rejected=false;
                try { (void)MacClosedWorld::from_checkpoint(setup.registry,setup.eos,corrupt); } catch (const std::exception&) { rejected=true; }
                require(rejected,"Malformed saved clock, geometry or trailing data was accepted");
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
            if (operation=="--write-checkpoint") {
                const auto image=owner(setup,false).checkpoint(); std::ofstream output(argv[2],std::ios::binary);
                output.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));
                require(output.good(),"Could not write the independent cross-platform checkpoint fixture");
            } else if (operation=="--read-checkpoint") {
                std::ifstream input(argv[2],std::ios::binary); require(input.good(),"Could not read the cross-platform checkpoint fixture");
                const std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(input),{}};
                require(MacClosedWorld::from_checkpoint(setup.registry,setup.eos,image).checkpoint()==image,
                    "Cross-platform checkpoint did not retain the complete original bytes");
            } else throw std::invalid_argument("Unknown checkpoint fixture operation");
        } else require(argc==1,"Invalid checkpoint test arguments");
        std::cout<<"PASS physical checkpoint, exact continuation, trace/passive/full momentum, model identity and malformed/truncated images\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
