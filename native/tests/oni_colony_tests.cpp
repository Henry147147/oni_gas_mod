// Colony owner control (docs/colony-owner.md). Made-up materials and
// inventories, not game data.
#include "../src/oni_colony.hpp"
#include "../src/oni_world_biology.hpp"
#include <functional>
#include <limits>
#include "../src/oni_precise_receipt.hpp"
#include "../src/oni_release_capture.hpp"
#include "../src/oni_transfer_capture.hpp"
#include "../src/oni_floor_capture.hpp"
#include "../src/oni_parcel_wire.hpp"
#include "../src/oni_parcel_outcome.hpp"
#include "../src/oni_precision_inventory.hpp"
#include "../src/oni_retirement_inventory.hpp"
#include <fstream>
#include <cstdlib>
#include "../src/oni_sha256.hpp"
#include "mixed_atmospheres/mac_kernel.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

void airflow_boundary_control();

namespace {
#if defined(_WIN32)
int setenv(const char* name,const char* value,int overwrite) {
    return !overwrite && std::getenv(name) ? 0 : _putenv_s(name,value);
}
int unsetenv(const char* name) { return _putenv_s(name,""); }
#endif
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
MAOniColonyMaterial material(const char* id, std::int32_t hash, std::uint32_t index, bool gas, double molar, double c, double density = 0) {
    MAOniColonyMaterial m{}; m.struct_size = sizeof(m); m.abi_version = MA_ONI_COLONY_ABI_VERSION; m.element_hash = hash; m.native_index = index;
    m.phase = gas ? 1 : 2; m.molar_mass_kg_per_mol = molar; m.specific_heat_j_per_kg_k = c; m.conductivity_w_per_m_k = gas ? .02 : .5;
    m.density_kg_per_m3 = density; m.low_temperature_k = gas ? 50 : 250; m.high_temperature_k = gas ? 1e4 : 700;
    std::snprintf(m.id, sizeof(m.id), "%s", id);
    return m;
}
constexpr std::uint32_t oxygen = 3, carbon = 4, water = 5, steam = 6, brine = 7, vacuum = 9, W = 16, H = 12, GW = 20, GH = 15, X0 = 2, Y0 = 1;
std::uint32_t public_cell(std::uint32_t x, std::uint32_t y) { return (X0 + x) + (Y0 + y) * GW; }
int native_cell(std::uint32_t x, std::uint32_t y) { return static_cast<int>((X0 + x + 1) + (Y0 + y + 1) * (GW + 2)); }
void falling_solid_control() {
    using namespace ma::oni::colony;
    // Real mixed gas, with a binary32 projection that cannot retain its
    // minority constituent. Model one observed native solid swap, then melt.
    for (const bool diagonal : {false, true}) for (const auto melt : {oxygen, carbon}) for (const bool heat_copy : {false, true}) for (const bool ambiguous : {false, true}) {
        MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=1;
        cfg.flags=12; cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3;
        cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
        std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),
            material("CarbonDioxide",12,carbon,true,.044,850)};
        std::vector<MAOniColonyCell> cells(9); cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,1,2000};
        if (ambiguous) cells[5]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0};
        start(cfg,mats,cells,1);
        const MAOniColonyTransfer add{4,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.2,300,0};
        queue_transfers(std::span(&add,1)); advance(1,.2,2); wait();
        std::array<std::vector<std::uint16_t>,2> element;
        std::array<std::vector<float>,2> mass,temperature;
        std::array<std::vector<std::uint8_t>,2> disease;
        std::array<std::vector<std::int32_t>,2> population;
        NativeCells native{}; native.pitch=native.rows=5;
        for(int b=0;b<2;++b) {
            element[b].assign(25,1); mass[b].assign(25,1000); temperature[b].assign(25,300);
            element[b][12]=vacuum; if (ambiguous) element[b][13]=vacuum;
            disease[b].assign(25,255); population[b].assign(25,0);
            native.element[b]=element[b].data(); native.mass[b]=mass[b].data(); native.temperature[b]=temperature[b].data();
            native.disease[b]=disease[b].data(); native.disease_count[b]=population[b].data();
        }
        const int source = diagonal ? 18 : 17;
        for(int b=0;b<2;++b) mass[b][static_cast<std::size_t>(source)]=4;
        write_shadow(native);
        if (!ambiguous) require(beside_owned(source,true) && beside_owned(source)==!diagonal,
            "solid observation must include diagonal-only ownership without widening the fluid fence");
        if (ambiguous) {element[1][13]=element[1][12]; mass[1][13]=mass[1][12]; temperature[1][13]=temperature[1][12];}
        disease[1][static_cast<std::size_t>(source)]=1; population[1][static_cast<std::size_t>(source)]=11;
        observe_native_solid(native,source);
        std::swap(element[1][12],element[1][static_cast<std::size_t>(source)]); std::swap(mass[1][12],mass[1][static_cast<std::size_t>(source)]);
        std::swap(temperature[1][12],temperature[1][static_cast<std::size_t>(source)]);
        std::swap(disease[1][12],disease[1][static_cast<std::size_t>(source)]); std::swap(population[1][12],population[1][static_cast<std::size_t>(source)]);
        element[1][12]=static_cast<std::uint16_t>(melt);
        if (ambiguous) {element[1][13]=static_cast<std::uint16_t>(melt); mass[1][13]=4;}

        const double expected_heat = heat_copy ? static_cast<double>(mass[1][static_cast<std::size_t>(source)])*1005*2 : 0;
        if (heat_copy) { temperature[1][static_cast<std::size_t>(source)] += 2; population[1][static_cast<std::size_t>(source)] += 7; } // native heat/biology followed the copy
        capture_shadow(native);
        if (ambiguous) {
            require(status().state==2 && std::strstr(status().error,"ambiguous")!=nullptr,"ambiguous solid displacement must fault before scalar adoption");
            stop(); continue;
        }
        advance(2,.2,2); wait();
        std::array<double,2> held{};
        for(unsigned c=0;c<9;++c) {
            std::uint32_t indices[4]{}; double masses[4]{};
            const auto count=composition(c,indices,masses,4);
            for(unsigned k=0;k<count;++k) held[indices[k]==oxygen?0:1]+=masses[k];
        }
        const std::array<std::uint32_t,9> all{0,1,2,3,4,5,6,7,8};
        MAOniColonyPassiveStamp stamp{}; std::vector<MAOniColonyPassiveRow> passives;
        require(read_passives(0,0,all,stamp,passives),"falling solid passive snapshot unavailable");
        std::array<double,2> germs{};
        for(const auto& row:passives) {require(row.identity==1 || row.identity==2,"falling solid changed passive identity"); germs[row.identity-1]+=row.quantity;}
        require(std::abs(germs[0]-(heat_copy?2007.:2000.))<1e-9 && std::abs(germs[1]-11)<1e-9,
            "falling solid lost native melt germs or displaced owned germs");
        const auto result=status();
        require(result.state==1 && std::abs(held[0]-(melt==oxygen?5.:1.))<1e-12 &&
            std::abs(held[1]-(melt==carbon?4.2:.2))<1e-12,"falling solid melt must preserve every owned constituent");
        require(result.native_mass_in_kg==4 && result.native_mass_out_kg==0 && result.native_heat_j==expected_heat,
            "falling solid books only real melt, never a displaced projection");
        require(result.maximum_relative_mass_error<1e-12 && result.maximum_relative_energy_error<1e-12,
            "falling solid mass and energy ledgers close");
        stop();
    }
    std::cout<<"PASS falling solid melt: constituents, heat, germs, ledgers, ambiguous endpoints refused\n";
}
void release_capture_control() {
    const auto path=(std::filesystem::temp_directory_path()/("ma-release-control-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    struct Cleanup { std::string path; ~Cleanup() { std::error_code ignored; std::filesystem::remove(path+".request",ignored); } } cleanup{path};
    MAOniColonyConfig cfg{}; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3;
    std::vector<std::uint8_t> walls(9); walls[4]=walls[5]=1; const auto initial=walls;
    ma::oni::colony::detail::ReleaseCapture capture; capture.configure(path.c_str());
    require(!capture.ready(cfg,walls),"release capture ran before marker publication");
    const auto request=[&](bool malformed) {
        ma::oni::colony::detail::PreciseReceiptWriter bytes;
        constexpr char magic[]="MAREL001"; bytes.bytes.insert(bytes.bytes.end(),magic,magic+8);
        for (const auto dimension : {3u,3u,0u,0u,3u,3u}) bytes.u32(dimension);
        bytes.u32(2); bytes.u32(4); bytes.u32(malformed?4u:5u);
        std::ofstream out(path+".request",std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.bytes.data()),static_cast<std::streamsize>(bytes.bytes.size()));
        require(out.good(),"could not prepare release marker control");
    };
    request(true); bool rejected=false;
    try { (void)capture.ready(cfg,walls); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected && walls==initial,"invalid release marker admitted or altered physics");
    request(false); require(!capture.ready(cfg,walls),"release capture ran while gates closed");
    walls[4]=0; require(!capture.ready(cfg,walls),"release capture ran with one gate still closed");
    walls[5]=0; const auto opened=walls; require(capture.ready(cfg,walls) && walls==opened,"release capture missed fully open gate or altered physics");
    capture.captured(); require(!capture.ready(cfg,walls),"release capture repeated after its first image");
    capture.configure(path.c_str()); auto incompatible=cfg; incompatible.grid_height=4; rejected=false;
    try { (void)capture.ready(incompatible,walls); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"release request geometry mismatch admitted");
    std::cout<<"PASS release capture marker: fresh, geometry-bound, all-gates-open, once only, no physical edits\n";
}
void exact_release_control() {
    using namespace ma::oni::colony;
    {
        ma::experimental::ReferenceMaterial material;material.phase=ma::experimental::ReferencePhase::ideal_gas;
        material.cv_j_per_kg_k=1001.3;material.energy_offset_j_per_kg=-300000.;
        ma::experimental::MacMaterialCell cell{ma::MixtureInventory(2),{}};
        const float mass=1.25f,temperature=353.15f;cell.mixture.masses[0]=mass;
        cell.mixture.energy=static_cast<double>(mass)*(material.energy_offset_j_per_kg+material.cv_j_per_kg_k*temperature);
        const double inverse=(cell.mixture.energy-static_cast<double>(mass)*material.energy_offset_j_per_kg)/
            (static_cast<double>(mass)*material.cv_j_per_kg_k);
        require(inverse!=temperature && std::nextafter(inverse,INFINITY)==temperature,"synthetic inverse EOS must differ by one binary64 ULP");
        require(detail::exact_initial_gas_cell(cell,0,mass,temperature,material),"exact forward caloric state was rejected by inverse rounding");
        auto heated=cell;heated.mixture.energy=std::nextafter(heated.mixture.energy,INFINITY);
        require(!detail::exact_initial_gas_cell(heated,0,mass,temperature,material),"one-ULP heat was accepted as the frozen initial state");
        heated=cell;heated.energy_remainder_j.projected=1e-240;
        require(!detail::exact_initial_gas_cell(heated,0,mass,temperature,material),"carried hidden heat was accepted as the frozen initial state");
        heated=cell;heated.mixture.masses[1]=1e-240;
        require(!detail::exact_initial_gas_cell(heated,0,mass,temperature,material),"a hidden constituent was accepted as the frozen initial state");
    }
    const auto path=(std::filesystem::temp_directory_path()/("ma-exact-release-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    const char* previous=std::getenv("MA_COLONY_RELEASE_DUMP");const bool had_previous=previous;
    const std::string saved=previous?previous:"";
    struct Cleanup {std::string path,saved;bool had;
        ~Cleanup(){stop();if(had)setenv("MA_COLONY_RELEASE_DUMP",saved.c_str(),1);else unsetenv("MA_COLONY_RELEASE_DUMP");
            std::error_code ignored;for(const auto suffix:{"",".request",".tmp"})std::filesystem::remove(path+suffix,ignored);}
    } cleanup{path,saved,had_previous};
    setenv("MA_COLONY_RELEASE_DUMP",path.c_str(),1);
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL;cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=7;cfg.grid_height=cfg.height=4;
    cfg.gravity_m_per_s2=9.81;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,997.125)};
    std::vector<MAOniColonyCell> cells(28,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    std::vector<MAOniColonyTransfer> edits;
    for(const bool hot:{true,false})for(unsigned y=1;y<=2;++y)for(unsigned x=hot?1:4;x<=(hot?2u:5u);++x) {
        const auto i=x+7*y;cells[i]={vacuum,MA_ONI_COLONY_FLUID,0,0,0,0};
        edits.push_back({i,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,hot?.5521116852760315:.6886040568351746,
            static_cast<double>(hot?353.15f:283.15f),0});
    }
    detail::PreciseReceiptWriter marker;constexpr char magic[]="MAREL002";marker.bytes.insert(marker.bytes.end(),magic,magic+8);
    for(const auto dimension:{7u,4u,0u,0u,7u,4u})marker.u32(dimension);
    marker.u32(static_cast<std::uint32_t>(edits.size()));
    for(const auto& e:edits){marker.u32(e.public_cell);marker.u32(e.native_index);marker.u32(std::bit_cast<std::uint32_t>(static_cast<float>(e.mass_kg)));
        marker.u32(std::bit_cast<std::uint32_t>(static_cast<float>(e.temperature_k)));}
    {std::ofstream file(path+".request",std::ios::binary);file.write(reinterpret_cast<const char*>(marker.bytes.data()),static_cast<std::streamsize>(marker.bytes.size()));}
    start(cfg,mats,cells,1);queue_transfers(std::span(edits).first(4));advance(1,.2,2);wait();
    require(status().state==1 && !std::filesystem::exists(path),"exact initial capture accepted a partial fill");
    queue_transfers(std::span(edits).subspan(4));advance(2,.2,2);wait();
    require(status().state==1 && !std::filesystem::exists(path),"exact initial capture accepted a previously evolved partial fill");
    stop();start(cfg,mats,cells,1);queue_transfers(edits);advance(1,.2,2);wait();
    require(status().state==1 && std::filesystem::exists(path),"exact initial capture missed a complete unevolved atomic fill");
    std::ifstream image(path,std::ios::binary|std::ios::ate);require(image.tellg()>static_cast<std::streamoff>(4+sizeof(MAOniColonyMaterial)+8+64),"exact initial image truncated");
    stop();std::cout<<"PASS exact initial capture: all eight fluid tuples before first flow; partial and evolved fills refused\n";
}
struct ReceiptReader {
    const std::vector<std::uint8_t>& bytes; std::size_t at = 0;
    bool observations=false;
    std::uint64_t integer(unsigned n) {
        require(at + n <= bytes.size(), "truncated precise receipt"); std::uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i) value |= static_cast<std::uint64_t>(bytes[at++]) << (8 * i);
        return value;
    }
    double f64() { return std::bit_cast<double>(integer(8)); }
    ma::experimental::MacEnergy scaled() {
        const auto mantissa = f64(); const auto n = integer(4);
        require(n <= 20000 && at + n <= bytes.size(), "bad precise exponent length");
        const std::string exponent(bytes.begin() + at, bytes.begin() + at + n); at += n;
        return ma::experimental::MacEnergy::from_parts(mantissa, ma::experimental::MacExponent::from_decimal(exponent));
    }
    std::uint32_t header(std::uint64_t sequence, std::uint64_t frame) {
        observations=bytes.size()>=28 && std::memcmp(bytes.data(),"MARECP02",8)==0;
        require(bytes.size() >= 28 && (observations || std::memcmp(bytes.data(), "MARECP01", 8) == 0), "precise wire magic"); at = 8;
        require(integer(8) == sequence && integer(8) == frame, "precise wire receipt identity");
        return static_cast<std::uint32_t>(integer(4));
    }
};
void floor_capture_control() {
    using namespace ma::oni::colony;
    const auto path=(std::filesystem::temp_directory_path()/("ma-floor-control-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    struct Cleanup {std::string path;~Cleanup(){std::error_code ignored;
        std::filesystem::remove(path,ignored);std::filesystem::remove(path+".request",ignored);}} cleanup{path};
    std::array<std::array<std::uint16_t,25>,2> elements{};
    std::array<std::array<float,25>,2> masses{},temperatures{};
    NativeCells source{};source.pitch=source.rows=5;
    for(unsigned b=0;b<2;++b){source.element[b]=elements[b].data();source.mass[b]=masses[b].data();source.temperature[b]=temperatures[b].data();}
    detail::FloorCapture capture;capture.configure(path.c_str());
    require(!capture.observe(source,6,.2),"floor capture ran before marker");
    detail::PreciseReceiptWriter marker;constexpr char magic[]="MAFLRQ01";marker.bytes.insert(marker.bytes.end(),magic,magic+8);
    for(const auto dimension:{3u,3u,0u,0u,3u,3u})marker.u32(dimension);
    marker.u32(1);marker.u32(4);marker.u32(oxygen);marker.u32(std::bit_cast<std::uint32_t>(1.2f));marker.u32(std::bit_cast<std::uint32_t>(300.f));
    {std::ofstream file(path+".request",std::ios::binary);file.write(reinterpret_cast<const char*>(marker.bytes.data()),static_cast<std::streamsize>(marker.bytes.size()));}
    require(!capture.observe(source,6,.2) && !std::filesystem::exists(path),"floor capture admitted stale native inputs");
    elements[0][12]=oxygen;masses[0][12]=1.2f;temperatures[0][12]=300;
    require(!capture.observe(source,7,.2) && !std::filesystem::exists(path),"floor capture accepted only the first native buffer");
    elements[1][12]=oxygen;masses[1][12]=1.2f;temperatures[1][12]=300;masses[0][12]=1.3f;
    require(!capture.observe(source,8,.2) && !std::filesystem::exists(path),"floor capture accepted only the second native buffer");
    masses[0][12]=1.2f;
    const auto before_elements=elements;const auto before_masses=masses;const auto before_temperatures=temperatures;
    require(capture.observe(source,9,.2),"floor capture missed exact pre-update fill in both native buffers");
    require(elements==before_elements && masses==before_masses && temperatures==before_temperatures,"floor capture mutated native arrays");
    std::ifstream file(path,std::ios::binary);const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(file),{});
    require(bytes.size()==84 && std::memcmp(bytes.data(),"MAFLPR01",8)==0,"floor capture framing");
    ReceiptReader row{bytes};row.at=32;
    require(row.integer(8)==9 && row.f64()==.2 && row.integer(4)==3 && row.integer(4)==1 && row.integer(4)==4,"floor capture source identity");
    require(row.integer(4)==oxygen && row.integer(4)==std::bit_cast<std::uint32_t>(1.2f) && row.integer(4)==std::bit_cast<std::uint32_t>(300.f),"floor capture matching array changed");
    require(row.integer(4)==oxygen && row.integer(4)==std::bit_cast<std::uint32_t>(1.2f) && row.integer(4)==std::bit_cast<std::uint32_t>(300.f) && row.at==bytes.size(),
        "floor capture second native buffer differs");
    require(!capture.observe(source,10,.2),"floor capture repeated after success");
    detail::FloorCapture again;again.configure(path.c_str());bool refused=false;
    try{(void)again.observe(source,11,.2);}catch(const std::runtime_error&){refused=true;}
    require(refused,"floor capture clobbered prior evidence");
    file.close(); // Windows cannot remove the captured path while a reader holds it.
    std::filesystem::remove(path);
    detail::FloorCapture contact;contact.configure(path.c_str());
    require(contact.observe(source,12,.2,1,12,13),"pre-contact floor capture missed the exact fill");
    std::ifstream contact_file(path,std::ios::binary);const std::vector<std::uint8_t> contact_bytes(std::istreambuf_iterator<char>(contact_file),{});
    require(contact_bytes.size()==96 && std::memcmp(contact_bytes.data(),"MAFLPR02",8)==0,"pre-contact capture framing");
    ReceiptReader phase{contact_bytes};phase.at=56;
    require(phase.integer(4)==1 && phase.integer(4)==12 && phase.integer(4)==13,"pre-contact capture phase/contact provenance");
    require(elements==before_elements && masses==before_masses && temperatures==before_temperatures,"pre-contact capture mutated native inputs");
    std::cout<<"PASS floor admission capture: exact fill, both native arrays, source ticket, no edits or overwrite\n";
}
void transfer_capture_control() {
    using namespace ma::oni::colony;
    const auto path=(std::filesystem::temp_directory_path()/("ma-transfer-control-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    const char* previous=std::getenv("MA_COLONY_TRANSFER_DUMP");
    struct Cleanup {
        std::string path, previous; bool had_previous;
        ~Cleanup() { stop(); if(had_previous) setenv("MA_COLONY_TRANSFER_DUMP",previous.c_str(),1);
            else unsetenv("MA_COLONY_TRANSFER_DUMP");
            std::error_code ignored;
            std::filesystem::remove(path,ignored); std::filesystem::remove(path+".request",ignored); }
    } cleanup{path,previous?previous:"",previous!=nullptr};
    require(setenv("MA_COLONY_TRANSFER_DUMP",path.c_str(),1)==0,"could not set transfer capture path");
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0};
    start(cfg,mats,cells,1);
    const MAOniColonyTransfer injection{4,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,1e308,1e308,0};
    queue_transfers(std::span(&injection,1)); advance(1,.2,2); wait();
    require(status().state==2,"unrepresentable transfer did not fault for capture control");
    const auto read=[](const std::string& name) {
        std::ifstream in(name,std::ios::binary); require(in.good(),"missing failed-transfer evidence");
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),{});
    };
    const auto image=read(path),request=read(path+".request");
    ReceiptReader world{image}; require(world.integer(4)==1,"capture material count");
    require(image.size()>4+sizeof(mats[0])+16 && std::memcmp(image.data()+4,mats.data(),sizeof(mats[0]))==0,"capture material records changed");
    world.at+=sizeof(mats[0]); require(world.f64()==.2,"capture attempted timestep changed");
    require(std::memcmp(image.data()+world.at,"MACCLS01",8)==0,"capture lacks replayable physical checkpoint");
    require(request.size()>=8 && std::memcmp(request.data(),"MATREQ01",8)==0,"capture request magic");
    ReceiptReader edit{request}; edit.at=8;
    require(edit.integer(8)==1 && edit.integer(4)==cfg.flags && edit.integer(4)==1,"capture frame/profile/topology changed");
    require(edit.integer(8)==4 && edit.integer(8)==0 && edit.integer(4)==1 && edit.integer(4)==0,"capture edit identity changed");
    require(edit.f64()==injection.mass_kg && edit.f64()==injection.temperature_k && edit.integer(4)==0 && edit.at==request.size(),"capture binary64 request changed");
    ma::experimental::MacFluidTransfer second{4,0,true,1,300};
    require(!detail::capture_transfer(path.c_str(),mats,{},.1,second,9,3),"capture overwrote existing private evidence");
    require(read(path)==image && read(path+".request")==request,"capture clobbered first failure");
    std::cout<<"PASS failed-transfer capture: exact pre-edit checkpoint/request, recorded flags, fresh paths, no clobber\n";
}
void precise_receipt_control() {
    using namespace ma::experimental;
    using namespace ma::oni::colony;
    MacFluidParcel parcel{}; parcel.injected = false;
    parcel.mass_kg = 1e-240; parcel.temperature_k = 311.1234567890123; parcel.internal_energy_j = -1e-230;
    parcel.resolved_kinetic_energy = MacEnergy::from_parts(.75, MacExponent::from_decimal("92233720368547758080"));
    parcel.potential_energy = MacEnergy::from_parts(-.625, MacExponent::from_decimal("-92233720368547758080"));
    parcel.momentum_x = MacEnergy::from_parts(.875, -2400); parcel.momentum_y = MacEnergy::from_double(-3.25);
    parcel.passives = {{std::uint64_t{1} << 63, 1e-230}, {7, .1234567890123}};
    detail::PreciseReceiptWriter wire; wire.header(123, 456, 1); wire.parcel(parcel, carbon, 987);
    ReceiptReader reader{wire.bytes}; require(reader.header(123,456) == 1, "precise parcel count");
    require(reader.integer(4) == carbon && reader.integer(4) == 987 && reader.integer(4) == 0, "precise parcel indices");
    require(reader.f64() == parcel.mass_kg && reader.f64() == parcel.temperature_k && reader.f64() == parcel.internal_energy_j,
        "precise binary64 mass, temperature or signed internal energy changed");
    for (const auto& value : {parcel.resolved_kinetic_energy, parcel.potential_energy, parcel.momentum_x, parcel.momentum_y}) {
        const auto decoded = reader.scaled();
        require(decoded.mantissa() == value.mantissa() && decoded.exponent() == value.exponent(), "precise scaled term was projected or rounded");
    }
    require(reader.integer(4) == 2, "precise passive count");
    for (const auto& passive : parcel.passives)
        require(reader.integer(8) == passive.identity && reader.f64() == passive.quantity, "precise passive identity/quantity changed");
    require(reader.at == wire.bytes.size(), "precise parcel trailing bytes");

    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,3,103};
    std::vector<std::uint8_t> expected; std::vector<MAOniColonyReceipt> old_receipts;
    for (const bool precise : {false,true}) {
        start(cfg,mats,cells,1); if (precise) enable_precise_receipts(true);
        const MAOniColonyTransfer injection{4,carbon,MA_ONI_COLONY_INJECT,0,71,4,.1234567890123,311.1234567890123,57};
        queue_transfers(std::span(&injection,1)); advance(1,.2,2); wait();
        const MAOniColonyTransfer removal{4,vacuum,MA_ONI_COLONY_EXTRACT_GAS,0,72,0,.33333333333333,0,0};
        queue_transfers(std::span(&removal,1)); advance(2,.2,2); wait();
        require(status().state==1, "precise receipt live owner fault");
        std::vector<MAOniColonyReceipt> got(8); got.resize(copy_receipts(got.data(),8));
        require(got.size()==3, "precise mixed extraction needs one receipt per species");
        if (!precise) { expected=checkpoint(); old_receipts=got; }
        else {
            require(checkpoint()==expected && std::memcmp(got.data(),old_receipts.data(),got.size()*sizeof(got[0]))==0,
                "precise receipt opt-in changed trajectory/checkpoint or legacy receipt bytes");
            enable_precise_receipts(true);
            bool rejected=false; try { enable_precise_receipts(false); } catch (const std::invalid_argument&) { rejected=true; }
            require(rejected, "precise mode change discarded outstanding sidecars");
        }
        for (const auto& r : got) {
            const auto size=copy_precise_receipt(r.sequence,nullptr,0);
            if (!precise) { require(size==0, "legacy mode allocated a sidecar"); continue; }
            require(size>28, "missing precise transfer receipt");
            std::vector<std::uint8_t> bytes(size,0xa5);
            require(copy_precise_receipt(r.sequence,bytes.data(),size-1)==size && std::all_of(bytes.begin(),bytes.end(),[](auto b){return b==0xa5;}),
                "undersized precise copy partially wrote or consumed receipt");
            require(copy_precise_receipt(r.sequence,bytes.data(),size)==size, "precise receipt copy size changed");
            ReceiptReader decoded{bytes}; require(decoded.header(r.sequence,r.completed_frame)==1, "precise phase receipt grouped wrong species");
            require(decoded.integer(4)==r.native_index && decoded.integer(4)==4 && decoded.integer(4)==(r.callback==71?1u:0u), "precise receipt parcel association");
            const auto mass=decoded.f64(); const auto temperature=decoded.f64(); const auto internal=decoded.f64();
            require(mass>0 && temperature>0 && std::isfinite(internal), "precise receipt physical fields");
            if (r.callback==71) require(mass==injection.mass_kg && temperature==injection.temperature_k && mass!=static_cast<double>(r.mass_kg),
                "precise injection rounded through legacy float packet");
        }
        ack_receipts(got.back().sequence);
        for (const auto& r : got) require(copy_precise_receipt(r.sequence,nullptr,0)==0, "acknowledged precise receipt retained");
        enable_precise_receipts(false); stop();
    }
    std::cout<<"PASS precise adapter receipts: scaled range, traces/passives, sequence/ack, opt-in byte-identical trajectory and legacy receipts\n";
}
void parcel_wire_control() {
    using namespace ma::oni::colony::detail;
    using ma::experimental::MacEnergy;using ma::experimental::MacExponent;
    MovingParcelInput item;item.public_cell=11;item.native_index=oxygen;item.mass_kg=.25;
    item.internal_energy_j=MacEnergy::from_parts(-.75,MacExponent::from_decimal("99999999999999999999999"));
    item.kinetic_energy_j=MacEnergy::from_parts(.5,MacExponent::from_decimal("-99999999999999999999999"));
    item.potential_energy_j=MacEnergy::from_double(-37);item.momentum_x=MacEnergy::from_double(12);item.momentum_y=MacEnergy::from_double(-13);
    item.passives={{1,1e-240},{UINT64_MAX,37.5}};
    auto other=item;other.native_index=carbon;other.mass_kg=.5;
    const std::array inputs{item,other};const auto wire=encode_moving_parcels(inputs);
    const auto decoded=decode_moving_parcels(wire);
    require(decoded.size()==2 && decoded[0].public_cell==11 && decoded[1].native_index==carbon &&
        decoded[0].internal_energy_j.exponent()==item.internal_energy_j.exponent() &&
        decoded[0].internal_energy_j.mantissa()==-.75 && decoded[0].passives[0].quantity==1e-240 &&
        decoded[0].passives[1].identity==UINT64_MAX && encode_moving_parcels(decoded)==wire,
        "moving parcel wire narrowed range, identities or canonical bytes");
    const auto rejects=[&](std::span<const std::uint8_t> bytes) {
        bool refused=false;try{(void)decode_moving_parcels(bytes);}catch(const std::invalid_argument&){refused=true;}
        require(refused,"malformed moving parcel wire admitted");
    };
    for(std::size_t size=0;size<wire.size();++size)rejects(std::span(wire).first(size));
    auto bad=wire;bad.push_back(0);rejects(bad);bad=wire;bad[0]='X';rejects(bad);
    MovingParcelInput zero;zero.public_cell=11;zero.native_index=oxygen;zero.mass_kg=1;
    const std::array zeros{zero};const auto basic=encode_moving_parcels(zeros);
    require(basic.size()==97 && basic[12]==11 && basic[13]==0 && basic[36]==1 && basic[40]=='0',"parcel wire is not canonical little endian");
    bad=basic;bad[35]=128;rejects(bad); // negative zero U
    bad=basic;bad[40]='1';rejects(bad); // zero with nonzero exponent
    bad=basic;bad[36]=0;rejects(bad); // empty exponent
    bad=basic;bad[36]=2;bad[40]='+';bad.insert(bad.begin()+41,'0');rejects(bad);
    bad=basic;bad.erase(bad.begin()+40);const std::string enormous(20000,'9');
    bad.insert(bad.begin()+40,enormous.begin(),enormous.end());bad[36]=0x20;bad[37]=0x4e;rejects(bad);
    bad=basic;for(unsigned i=0;i<8;++i)bad[20+i]=0;rejects(bad); // zero mass
    bad=basic;bad[27]=0x7f;bad[26]=0xf8;rejects(bad); // NaN mass
    const auto invalid=[&](MovingParcelInput value) {
        bool refused=false;try{const std::array values{value};(void)encode_moving_parcels(values);}catch(const std::invalid_argument&){refused=true;}
        require(refused,"invalid native moving parcel producer admitted");
    };
    auto wrong=zero;wrong.kinetic_energy_j=MacEnergy::from_double(-1);invalid(wrong);
    wrong=zero;wrong.passives={{1,1},{1,2}};invalid(wrong);
    wrong=zero;wrong.passives={{0,1}};invalid(wrong);
    wrong=zero;wrong.passives={{1,0}};invalid(wrong);
    bool refused=false;try{const std::array duplicate{item,item};(void)encode_moving_parcels(duplicate);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"duplicate parcel identity admitted");
    std::vector<std::uint8_t> oversized(maximum_parcel_wire_bytes+1);rejects(oversized);
    std::cout<<"MOVING PARCEL WIRE PASS\n";
}
void parcel_outcome_control() {
    using namespace ma::oni::colony::detail;
    using ma::experimental::MacEnergy;using ma::experimental::MacExponent;
    MovingParcelOutcome outcome;outcome.sequence=1;outcome.frame=2;outcome.token=3;outcome.session=4;
    MovingParcelInput input;input.public_cell=11;input.native_index=oxygen;input.mass_kg=1;
    input.internal_energy_j=MacEnergy::from_double(-37);
    input.momentum_x=MacEnergy::from_parts(.75,MacExponent::from_decimal("99999999999999999999"));
    input.passives={{1,1e-240},{UINT64_MAX,37.5}};
    outcome.consumed.push_back(input);
    ParcelGridCredit credit;credit.mass_kg.add(1);credit.mass_kg.add(-1e-100);
    credit.internal_energy_j.add(input.internal_energy_j);credit.internal_energy_j.add(1e-200);
    credit.momentum_x.add(input.momentum_x);credit.momentum_x.add(-12);
    credit.kinetic_mixing_heat_j.add(7);credit.potential_work_j.add(-3);
    credit.wall_impulse_y=MacEnergy::from_double(-13);
    ParcelPassiveCredit passive;passive.identity=1;passive.quantity.add(1e-240);passive.quantity.add(-1e-300);
    credit.passives.push_back(passive);credit.passives_before={{1,1e-300}};credit.passives_after={{1,1e-240}};
    outcome.grid.push_back(credit);outcome.energy_residual_j=MacEnergy::from_double(-1e-200);
    outcome.energy_bound_j=MacEnergy::from_double(1e-190);
    const auto wire=encode_moving_outcome(outcome);const auto got=decode_moving_outcome(wire);
    require(encode_moving_outcome(got)==wire && got.consumed[0].mass_kg==1 && got.grid[0].mass_kg.size()==2,
        "moving outcome lost nominal source or exact grid tail");
    const auto same_sum=[](const auto& a,const auto& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)if(a.component(i).mantissa()!=b.component(i).mantissa() ||
            a.component(i).exponent()!=b.component(i).exponent())return false;
        return true;
    };
    require(same_sum(got.grid[0].mass_kg,credit.mass_kg) && same_sum(got.grid[0].internal_energy_j,credit.internal_energy_j) &&
        same_sum(got.grid[0].momentum_x,credit.momentum_x) && same_sum(got.grid[0].passives[0].quantity,passive.quantity) &&
        same_sum(got.grid[0].potential_work_j,credit.potential_work_j) && got.grid[0].passives_before[0].quantity==1e-300 &&
        got.grid[0].passives_after[0].quantity==1e-240 && got.grid[0].wall_impulse_y.mantissa()==credit.wall_impulse_y.mantissa(),
        "moving outcome narrowed mechanics, signed work or passive changes");
    const auto rejects=[](std::span<const std::uint8_t> bytes) {
        bool rejected=false;try{(void)decode_moving_outcome(bytes);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"malformed moving outcome admitted");
    };
    for(std::size_t n=0;n<wire.size();++n)rejects(std::span(wire).first(n));
    auto bad=wire;bad.push_back(0);rejects(bad);bad=wire;bad[0]='X';rejects(bad);
    bad=wire;bad[8]=0;rejects(bad);bad=wire;bad[40]=5;rejects(bad);
    bad=wire;bad[44]=255;bad[45]=255;rejects(bad);
    MovingParcelOutcome refusal;refusal.sequence=1;refusal.frame=2;refusal.token=3;refusal.session=4;
    refusal.refusal=17;refusal.reason="receipt capacity";
    const auto refused_wire=encode_moving_outcome(refusal);const auto refused=decode_moving_outcome(refused_wire);
    require(refused.grid.empty() && refused.consumed.empty() && refused.refusal==17 && encode_moving_outcome(refused)==refused_wire,
        "moving refusal consumed source or did not roundtrip");
    const auto invalid=[](MovingParcelOutcome value) {
        bool rejected=false;try{(void)encode_moving_outcome(value);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid moving outcome producer admitted");
    };
    auto wrong=outcome;wrong.grid.clear();invalid(wrong);
    wrong=outcome;wrong.refusal=1;wrong.reason="refused";invalid(wrong);
    wrong=outcome;wrong.energy_bound_j=MacEnergy::from_double(-1);invalid(wrong);
    wrong=outcome;wrong.grid[0].passives.push_back(passive);invalid(wrong);
    wrong=outcome;wrong.grid[0].passives_before={{0,1}};invalid(wrong);
    wrong=refusal;wrong.reason.clear();invalid(wrong);
    std::cout<<"MOVING PARCEL OUTCOME PASS\n";
}
void parcel_context_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1000)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=8;cfg.grid_height=9;cfg.offset_x=2;cfg.offset_y=3;
    cfg.width=cfg.height=3;cfg.gravity_m_per_s2=3.75;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    start(cfg,mats,cells,1);const auto image=checkpoint();const auto context=parcel_context();
    require(context.struct_size==72 && context.abi_version==1 && context.reserved==0 && context.flags==cfg.flags &&
        context.grid_width==8 && context.grid_height==9 && context.offset_x==2 && context.offset_y==3 &&
        context.width==3 && context.height==3 && context.cell_width_m==1 && context.cell_height_m==1 &&
        context.gravity_m_per_s2==3.75 && context.session==status().session && checkpoint()==image,
        "parcel context changed state or lost saved physical coordinates");
    stop();restore(cfg,mats,image,1);const auto restored=parcel_context();
    require(restored.session!=context.session && restored.offset_y==context.offset_y &&
        restored.gravity_m_per_s2==context.gravity_m_per_s2 && restored.cell_height_m==context.cell_height_m && checkpoint()==image,
        "parcel context did not follow restored session/coordinates");
    stop();std::cout<<"PARCEL CONTEXT PASS\n";
}
void passive_observation_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1000)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=8;cfg.grid_height=9;cfg.offset_x=2;cfg.offset_y=3;cfg.width=cfg.height=3;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,3,103};
    start(cfg,mats,cells,1);enable_precise_receipts(true);
    const std::uint32_t centre=35;const MAOniColonyTransfer injection{centre,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.125,300,0};
    const std::array<MAOniColonyPassive,4> passives{{{1,.25},{2,1e-240},{4,1e30},{ma::radiation_passive_identity,.5}}};
    (void)queue_pipe_job(1,injection,passives);advance(1,.2,2);wait();require(status().state==1,"passive observation source fault");
    MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1,"passive source receipt missing");ack_receipts(receipt.sequence);
    const auto image=checkpoint();const auto original_session=status().session;
    const std::array<std::uint32_t,3> selected{26,centre,44};
    MAOniColonyPassiveStamp stamp{};std::vector<MAOniColonyPassiveRow> rows;
    require(read_passives(0,0,selected,stamp,rows) && rows.size()==5 && stamp.struct_size==32 && stamp.abi_version==1 &&
        stamp.session==original_session && stamp.completed_frame==1 && stamp.time_seconds==status().time_seconds,"passive snapshot labels/counts differ");
    const std::array<std::uint64_t,5> ids{1,2,3,4,ma::radiation_passive_identity};const std::array<double,5> quantities{.25,1e-240,103,1e30,.5};
    for(std::size_t i=0;i<rows.size();++i)require(rows[i].public_cell==centre && rows[i].reserved==0 && rows[i].identity==ids[i] &&
        rows[i].quantity==quantities[i],"passive reader lost minority, wide count, reserved metadata or order");
    const auto snapshot=rows;
    const auto unchanged=[&] {
        require(rows.size()==snapshot.size() && std::memcmp(rows.data(),snapshot.data(),rows.size()*sizeof(rows[0]))==0 &&
            stamp.session==original_session && stamp.completed_frame==1,"failed passive read changed caller outputs");
    };
    require(!read_passives(original_session+1,1,selected,stamp,rows),"wrong passive session admitted");unchanged();
    require(!read_passives(original_session,2,selected,stamp,rows),"wrong passive frame admitted");unchanged();
    for(const auto& request : {std::vector<std::uint32_t>{centre,26},std::vector<std::uint32_t>{centre,centre},std::vector<std::uint32_t>{UINT32_MAX},std::vector<std::uint32_t>{}}) {
        bool refused=false;try{(void)read_passives(0,0,request,stamp,rows);}catch(const std::invalid_argument&){refused=true;}
        require(refused,"invalid passive selection admitted");unchanged();
    }
    require(checkpoint()==image,"passive observation changed checkpoint bytes");
    stop();restore(cfg,mats,image,1);
    require(!read_passives(original_session,1,selected,stamp,rows),"cold equal-frame passive session did not invalidate");unchanged();
    require(read_passives(0,0,selected,stamp,rows) && stamp.session!=original_session && rows.size()==snapshot.size() &&
        std::memcmp(rows.data(),snapshot.data(),rows.size()*sizeof(rows[0]))==0 && checkpoint()==image,"cold passive snapshot changed inventories or image");
    const std::array<std::uint32_t,1> empty_cell{26};require(read_passives(stamp.session,1,empty_cell,stamp,rows) && rows.empty(),"empty passive selection did not retire old rows");
    stop();std::cout<<"WORLD PASSIVE OBSERVATION PASS\n";
}
void quiet_game_cell_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL;cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    start(cfg,mats,cells,1);advance(1,.2,2);wait();require(status().state==1 && status().completed_frames==1,"quiet single-cell game owner failed");
    // An outcome the adapter cannot settle faults the owner like a failed
    // frame: it keeps the first reason and advances no further.
    bool refused=false; try { fault_from_adapter(""); } catch (const std::invalid_argument&) { refused=true; }
    require(refused && status().state==1,"an empty adapter fault was accepted");
    fault_from_adapter("pipe outcome refused");fault_from_adapter("second reason");
    advance(2,.2,2);wait();const auto faulted=status();
    require(faulted.state==2 && faulted.completed_frames==1 && std::string(faulted.error)=="Adapter: pipe outcome refused","adapter fault did not stop the owner");
    stop();refused=false; try { fault_from_adapter("after stop"); } catch (const std::invalid_argument&) { refused=true; }
    require(refused,"an adapter fault without an owner was accepted");
    std::cout<<"QUIET GAME CELL PASS\n";
}
void shadow_handoff_control() {
    using namespace ma::oni::colony;
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1000)};
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    start(cfg,mats,cells,1);
    std::array<std::vector<std::uint16_t>,2> elements;
    std::array<std::vector<float>,2> masses,temperatures;
    NativeCells native{};native.pitch=native.rows=5;
    for(int b=0;b<2;++b) {
        elements[b].assign(25,1);elements[b][12]=oxygen;
        masses[b].assign(25,0);temperatures[b].assign(25,300);
        native.element[b]=elements[b].data();native.mass[b]=masses[b].data();native.temperature[b]=temperatures[b].data();
    }
    const MAOniColonyTransfer injection{4,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.25,400,0};
    queue_transfers(std::span(&injection,1));advance(1,1,2);
    // A native frame may already be queued before managed pacing sees the
    // owner become busy. Its shadow must use the preceding completed frame,
    // even when the caller has not waited for that asynchronous advance.
    write_shadow(native);
    const auto completed=status();
    require(completed.state==1 && completed.completed_frames==1,
        "Native shadow returned before the preceding owner frame completed");
    std::array<MAOniColonyCellView,9> views{};MAOniColonyStatus view_status{};copy(view_status,views.data(),9);
    for(int b=0;b<2;++b)
        require(masses[b][12]==views[4].mass_kg && temperatures[b][12]==views[4].temperature_k && masses[b][12]==1.25f,
            "Native shadow used stale mass or temperature from a queued owner frame");
    const auto image=checkpoint();wait();
    require(checkpoint()==image,"Shadow synchronization advanced an additional physical frame");
    stop();std::cout<<"PASS native shadow handoff: preceding owner frame completed, both arrays current, no extra physical frame\n";
}
void moving_job_control() {
    using namespace ma::oni::colony;
    using namespace ma::oni::colony::detail;
    using ma::experimental::MacEnergy;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    start(cfg,mats,cells,1);const auto old=checkpoint();require(old[8]==8,"unused moving facility changed checkpoint version");
    const auto first_session=status().session;
    MovingParcelInput item;item.public_cell=4;item.native_index=carbon;item.mass_kg=.125;
    item.internal_energy_j=MacEnergy::from_double(.125*850*330);item.kinetic_energy_j=MacEnergy::from_double(.0625);
    item.potential_energy_j=MacEnergy::from_double(.125*9.81*1.5);item.momentum_x=MacEnergy::from_double(.125);
    item.passives={{3,.125},{7,1e-240}};
    const std::array items{item};const auto wire=encode_moving_parcels(items);
    const auto rejects=[&](auto action) {bool refused=false;try{action();}catch(const std::invalid_argument&){refused=true;}require(refused,"invalid moving admission accepted");};
    rejects([&]{(void)queue_moving_parcel(first_session,1,wire);});enable_precise_receipts(true);
    rejects([&]{(void)queue_moving_parcel(first_session+1,1,wire);});
    rejects([&]{(void)queue_moving_parcel(first_session,2,wire);});
    require(queue_moving_parcel(first_session,1,wire)==std::pair<std::uint32_t,std::uint64_t>{1,0},"moving job not queued");
    require(queue_moving_parcel(first_session,1,wire).first==1,"moving pending retry duplicated");
    auto changed=items;changed[0].mass_kg=.25;const auto different=encode_moving_parcels(changed);
    rejects([&]{(void)queue_moving_parcel(first_session,1,different);});
    MAOniColonyTransfer legacy{4,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.125,330,0};
    rejects([&]{(void)queue_pipe_job(1,legacy,{});});
    const auto pending=checkpoint();require(pending[8]==9 && copy_moving_outcome(1,nullptr,0)==0,"moving durability/publication framing");
    const auto corrupt_pending=[&](bool marker_damage) {
        auto bad=pending;constexpr std::string_view magic="MAPARC01";
        const auto payload=std::search(bad.begin(),bad.end(),magic.begin(),magic.end());require(payload!=bad.end(),"saved moving payload absent");
        const auto at=static_cast<std::size_t>(payload-bad.begin());
        if(marker_damage)bad[at+12]^=1;else {bad[at+8]=0;bad[at+9]=0;bad[at+10]=0;bad[at+11]=0;}
        const auto digest=ma::oni::hash_memory(bad.data(),bad.size()-64);std::copy(digest.begin(),digest.end(),bad.end()-64);
        rejects([&]{restore(cfg,mats,bad,1);});require(checkpoint()==pending,"invalid moving restore replaced live colony");
    };
    corrupt_pending(false);corrupt_pending(true);
    const auto finish=[&] {
        advance(1,.2,2);wait();require(status().state==1,"moving job faulted owner");
        MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1 && receipt.kind==MA_ONI_COLONY_MOVING_PARCEL &&
            receipt.mass_kg==0 && receipt_job(receipt.sequence)==1 && pipe_job_state(1).first==2,"moving receipt/token association");
        rejects([&]{(void)checkpoint();});
        const auto size=copy_moving_outcome(receipt.sequence,nullptr,0);require(size>0,"moving outcome missing");
        std::vector<std::uint8_t> bytes(size,0xcd);copy_moving_outcome(receipt.sequence,bytes.data(),size-1);
        require(std::all_of(bytes.begin(),bytes.end(),[](auto v){return v==0xcd;}),"undersized moving buffer was modified");
        copy_moving_outcome(receipt.sequence,bytes.data(),size);const auto result=decode_moving_outcome(bytes);
        require(!result.refusal && result.session==status().session && result.token==1 && result.consumed.size()==1 &&
            result.consumed[0].public_cell==4 && result.consumed[0].native_index==carbon && result.consumed[0].mass_kg==.125 &&
            result.consumed[0].passives[1].quantity==1e-240 && result.grid[0].mass_kg.sign()>0,"moving result lost source/grid/passives");
        ack_receipts(receipt.sequence);require(copy_moving_outcome(receipt.sequence,nullptr,0)==0 && pipe_job_state(1).first==3,"moving acknowledgement leaked outcome");
        require(status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-12,"moving owner ledger does not close");
        return checkpoint();
    };
    const auto expected=finish();stop();restore(cfg,mats,pending,1);
    require(checkpoint()==pending && status().session!=first_session,"moving pending restore changed bytes/session");
    rejects([&]{(void)queue_moving_parcel(first_session,1,wire);});
    require(queue_moving_parcel(status().session,1,wire).first==1,"restored moving token not recognized");
    require(finish()==expected,"moving pending cold continuation differs");stop();
    // Unsupported mechanics refuse as a completed outcome. Compare against
    // a no-input continuation to prove no rejected candidate escaped.
    start(cfg,mats,cells,1);advance(1,.2,2);wait();const auto no_input=checkpoint();stop();
    start(cfg,mats,cells,1);enable_precise_receipts(true);
    auto invalid=items;invalid[0].kinetic_energy_j={};const auto invalid_wire=encode_moving_parcels(invalid);
    (void)queue_moving_parcel(status().session,1,invalid_wire);advance(1,.2,2);wait();
    require(status().state==1,"normal moving refusal faulted owner");MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1,"refusal receipt missing");
    std::vector<std::uint8_t> refused_wire(copy_moving_outcome(receipt.sequence,nullptr,0));
    copy_moving_outcome(receipt.sequence,refused_wire.data(),static_cast<std::uint32_t>(refused_wire.size()));
    const auto refused=decode_moving_outcome(refused_wire);require(refused.refusal && refused.grid.empty() && refused.consumed.empty(),"refused moving job consumed source");
    ack_receipts(receipt.sequence);require(status().injected_kg==0,"refused moving job booked mass");
    // Owner metadata differs (durable token/version/counter), physical image must not.
    const auto image=checkpoint();constexpr std::string_view core_magic="MACCLS01";
    const auto physical=[&](const auto& bytes) {return std::search(bytes.begin(),bytes.end(),core_magic.begin(),core_magic.end());};
    const auto a=physical(image),b=physical(no_input);
    require(a!=image.end() && b!=no_input.end() && std::equal(a,image.end()-64,b,no_input.end()-64),"refused moving candidate changed physical state");
    stop();
    for(const auto flags : {0u,MA_ONI_COLONY_GAME_KERNEL}) {
        cfg.flags=flags;start(cfg,mats,cells,1);enable_precise_receipts(true);
        auto left=item;left.native_index=oxygen;left.internal_energy_j=MacEnergy::from_double(.125*1000*330);
        left.momentum_x=MacEnergy::from_double(-.125);left.passives={{3,.25}};
        const std::array mixture{left,item};const auto mixed_wire=encode_moving_parcels(mixture);
        (void)queue_moving_parcel(status().session,1,mixed_wire);advance(1,.2,2);wait();
        require(status().state==1,"physical mixed moving batch faulted owner");
        require(copy_receipts(&receipt,1)==1 && copy_receipts(nullptr,0)==1,"physical mixture fragmented into unrelated receipts");
        std::vector<std::uint8_t> bytes(copy_moving_outcome(receipt.sequence,nullptr,0));
        copy_moving_outcome(receipt.sequence,bytes.data(),static_cast<std::uint32_t>(bytes.size()));
        const auto result=decode_moving_outcome(bytes);
        require(!result.refusal && result.consumed.size()==2 && result.grid.size()==2 &&
            result.consumed[0].native_index==oxygen && result.consumed[1].native_index==carbon &&
            result.grid[0].wall_impulse_x.mantissa()!=0 && result.grid[1].wall_impulse_x.mantissa()!=0 &&
            status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-12,
            "mixed moving game/reference receipt lost order, wall impulse or conservation");
        ack_receipts(receipt.sequence);stop();
        start(cfg,mats,cells,1);enable_precise_receipts(true);
        auto unknown=mixture;unknown[1].native_index=65535;const auto unknown_wire=encode_moving_parcels(unknown);(void)queue_moving_parcel(status().session,1,unknown_wire);advance(1,.2,2);wait();
        require(status().state==1 && status().injected_kg==0 && copy_receipts(&receipt,1)==1,"unknown minority admitted part of moving mixture");
        bytes.resize(copy_moving_outcome(receipt.sequence,nullptr,0));copy_moving_outcome(receipt.sequence,bytes.data(),static_cast<std::uint32_t>(bytes.size()));const auto denied=decode_moving_outcome(bytes);require(denied.refusal==1 && denied.consumed.empty() && denied.grid.empty(),"unknown moving minority not atomically refused");ack_receipts(receipt.sequence);stop();
    }
    std::cout<<"MOVING PARCEL DURABILITY PASS\n";
}
void tiny_injection_neighbour_control() {
    using namespace ma::oni::colony;
    const std::array mats{material("MadeUpGas",111,oxygen,true,.032,1005)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=5;cfg.grid_height=cfg.height=5;cfg.maximum_timestep_s=.1;
    // A 2^-54 kg residue cannot change 0.6 kg, but is one ULP at 0.25 kg.
    // The closed-source controls distinguish the original source radius from
    // an incorrectly widened search around its first open neighbour.
    for (int mode=0;mode<5;++mode) {
        std::array<MAOniColonyCell,25> cells{};
        for(auto& cell:cells)cell={oxygen,MA_ONI_COLONY_FLUID,.6f,300,0,0};
        if(mode==2 || mode==3)cells[12]={0,MA_ONI_COLONY_SOLID,0,0,0,0};
        const unsigned low_cell=mode==0?11u:mode==2?5u:mode==3?7u:mode==4?12u:25u;
        const bool accepts=mode==0 || mode==3 || mode==4;
        const unsigned expected_cell=mode==0?11u:mode==3?7u:mode==2?11u:12u;
        start(cfg,mats,cells,1);enable_precise_receipts(true);
        // Import equilibrates connected gas. Empty then fill the lower-mass
        // cell through real inputs before the tiny job, with no intervening
        // flow, so it holds exactly0.25 rather than an import-rounding value.
        // Cell5 is outside the closed source's radius two.
        double bulk_mass=0;
        if(low_cell<cells.size()) {
            const std::array<MAOniColonyTransfer,2> staging{{
                {low_cell,oxygen,MA_ONI_COLONY_EXTRACT,0,-1,0,1,0,0},
                {low_cell,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.25,300,0}}};
            queue_transfers(staging);bulk_mass=.25;
        }
        const MAOniColonyTransfer injection{12,oxygen,MA_ONI_COLONY_INJECT,2,-1,0,0x1p-54,300,0};
        const std::array<MAOniColonyPassive,1> passives{{{17,.125}}};
        (void)queue_pipe_job(1,injection,passives);const auto pending=checkpoint();
        const auto outcome=[&] {
            advance(1,.2,2);wait();const auto state=status();
            require(state.state==1 && state.completed_frames==1,"tiny native injection faulted");
            MAOniColonyReceipt receipt{};
            require(copy_receipts(&receipt,1)==1 && receipt_job(receipt.sequence)==1,"tiny injection lost token receipt");
            std::vector<std::uint8_t> wire(copy_precise_receipt(receipt.sequence,nullptr,0));
            copy_precise_receipt(receipt.sequence,wire.data(),static_cast<std::uint32_t>(wire.size()));
            ReceiptReader r{wire};require(r.header(receipt.sequence,1)==1 && r.observations,"tiny injection lacks precise observation");
            const auto element=r.integer(4),destination=r.integer(4),direction=r.integer(4);
            if(element!=oxygen || destination!=expected_cell || direction!=1)
                std::cerr<<"tiny injection mode "<<mode<<": destination "<<destination<<", expected "<<expected_cell<<", element "<<element<<", direction "<<direction<<'\n';
            require(element==oxygen && destination==expected_cell && direction==1,"tiny injection escaped source radius or changed destination");
            const double admitted=r.f64();
            if(admitted!=(accepts?injection.mass_kg:0))
                std::cerr<<"tiny injection mode "<<mode<<": admitted "<<std::hexfloat<<admitted<<", requested "<<injection.mass_kg<<std::defaultfloat<<'\n';
            require(admitted==(accepts?injection.mass_kg:0),"tiny injection did not preserve exact source remainder");
            require(r.f64()==(accepts?300:0),"tiny injection temperature differs");(void)r.f64();
            for(int i=0;i<4;++i)(void)r.scaled();
            require(r.integer(4)==(accepts?1u:0u),"refused tiny injection moved passives");
            if(accepts)require(r.integer(8)==17 && r.f64()==.125,"tiny injection lost supplied passive");
            require(r.integer(4)==(accepts?1u:0u),"tiny injection passive observations differ");
            if(accepts)require(r.integer(8)==17 && r.f64()==0 && r.f64()==.125,"tiny injection observed wrong destination passives");
            require(r.at==wire.size() && state.injected_kg==bulk_mass+admitted && state.trace_sink_kg==0 &&
                state.maximum_relative_mass_error<1e-12 && state.maximum_relative_energy_error<1e-12,
                "tiny injection changed receipts, sinks or conservation bounds");
            ack_receipts(receipt.sequence);require(pipe_job_state(1).first==3,"tiny injection token did not retire");
            return wire;
        };
        const auto expected=outcome();const auto settled=checkpoint();stop();
        restore(cfg,mats,pending,1);enable_precise_receipts(true);
        require(outcome()==expected && checkpoint()==settled,"tiny injection queued cold continuation differs");stop();
    }
    std::cout<<"PASS tiny native injection: exact neighbour admission, unchanged first destination, source radius, refusal, passives and cold continuation\n";
}
void precision_inventory_control() {
    using namespace ma::oni::colony;using namespace ma::oni::colony::detail;using namespace ma::experimental;
    const std::array mats{material("MadeUpGas",111,oxygen,true,.032,1005)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=5;cfg.grid_height=cfg.height=5;cfg.maximum_timestep_s=.1;
    std::array<MAOniColonyCell,25> cells{};for(auto& cell:cells)cell={oxygen,MA_ONI_COLONY_FLUID,.6f,300,0,0};
    cells[12]={0,MA_ONI_COLONY_SOLID,0,0,0,0};
    start(cfg,mats,cells,1);enable_precise_receipts(true);
    const auto unused=checkpoint();require(unused[8]==12,"new game owner did not retain boundary-law framing");
    const MAOniColonyTransfer injection{12,oxygen,MA_ONI_COLONY_INJECT,2,-1,0,0x1p-54,300,0};
    (void)queue_pipe_job(1,injection,{});const auto queued=checkpoint();
    const auto accept=[&] {
        advance(1,.2,2);wait();require(status().state==1,"precision ownership acceptance faulted");
        MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1 && receipt_job(receipt.sequence)==1,"precision ownership lost receipt");
        std::vector<std::uint8_t> bytes(copy_precise_receipt(receipt.sequence,nullptr,0));
        copy_precise_receipt(receipt.sequence,bytes.data(),static_cast<std::uint32_t>(bytes.size()));
        ReceiptReader r{bytes};require(r.header(receipt.sequence,1)==1,"precision receipt parcel count");
        require(r.integer(4)==oxygen && r.integer(4)==11 && r.integer(4)==1,"precision receipt provenance");
        require(r.f64()==injection.mass_kg && r.f64()==300,"precision receipt mass/temperature");
        MacEnergySum received;received.add(r.f64());received.add(r.scaled());received.add(r.scaled());
        require(r.scaled().is_zero() && r.scaled().is_zero() && r.integer(4)==0 && r.integer(4)==0 && r.at==bytes.size(),"precision receipt carries unsupported momentum/passives");
        const auto held=decode_precision_inventory(precision_inventory());
        require(held.size()==1 && held[0].mass_kg==injection.mass_kg && held[0].source==12 && held[0].anchor==11 && held[0].radius==2 && held[0].temperature_k==300,"precision inventory is not physical ownership");
        add_precision_sum(received,held[0].energy_j,-1);require(received.sign()==0,"held energy differs from exact receipt credit");
        require(status().injected_kg==injection.mass_kg && status().trace_sink_kg==0,"precision acceptance lost or double-booked input");
        ack_receipts(receipt.sequence);require(pipe_job_state(1).first==3,"accepted source job did not retire");
        return bytes;
    };
    const auto receipt=accept(),held_wire=precision_inventory(),accepted=checkpoint();
    require(accepted[8]==12,"precision inventory changed the new owner boundary law");
    if(const char* path=std::getenv("MA_PRECISION_TEST_IMAGE")) {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(accepted.data()),static_cast<std::streamsize>(accepted.size()));require(bool(f),"cannot write native controlled image");}
    stop();restore(cfg,mats,queued,1);enable_precise_receipts(true);
    require(accept()==receipt && checkpoint()==accepted,"queued precision acceptance cold replay differs");
    for(std::uint64_t frame=2;frame<=4;++frame){advance(frame,.2,2);wait();require(status().state==1 && precision_inventory()==held_wire && status().trace_sink_kg==0,"unrepresentable held inventory vanished or changed");}
    const auto cold=checkpoint();stop();restore(cfg,mats,cold,1);
    require(checkpoint()==cold && precision_inventory()==held_wire,"held inventory cold resave differs");
    // An available cell outside the ORIGINAL source radius must not release it.
    const auto stage=[&](unsigned cell) {
        const std::array<MAOniColonyTransfer,2> inputs{{{cell,oxygen,MA_ONI_COLONY_EXTRACT,0,-1,0,1,0,0},
            {cell,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.25,300,0}}};queue_transfers(inputs);
    };
    stage(5);advance(5,.000001,2);wait();
    require(status().state==1 && decode_precision_inventory(precision_inventory()).front().mass_kg==injection.mass_kg,"precision release widened source radius");
    stage(7);const auto before_release=checkpoint();
    const auto release=[&] {
        advance(6,.000001,2);wait();require(status().state==1,"precision late release faulted");
        const auto held=decode_precision_inventory(precision_inventory());
        require(std::all_of(held.begin(),held.end(),[](const auto& p){return p.mass_kg==0;}),"legal representable recipient did not release held mass");
        require(copy_receipts(nullptr,0)==0 && status().trace_sink_kg==0 && status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-12,"internal release rebooked an input or lost conservation");
        return checkpoint();
    };
    const auto released=release(),tail=precision_inventory();stop();restore(cfg,mats,before_release,1);
    require(release()==released && precision_inventory()==tail,"late precision release cold continuation differs");stop();restore(cfg,mats,released,1);
    require(checkpoint()==released && precision_inventory()==tail,"zero-mass signed compensation failed cold resave");stop();
    // Exact coalescing can make a previously unsupported ULP representable.
    restore(cfg,mats,accepted,1);enable_precise_receipts(true);
    (void)queue_pipe_job(2,injection,{});advance(2,.2,2);wait();
    require(status().state==1,"coalesced precision source faulted");
    const auto coalesced=decode_precision_inventory(precision_inventory());
    require(coalesced.size()<=1 && std::all_of(coalesced.begin(),coalesced.end(),[](const auto& p){return p.mass_kg==0;}) &&
        status().injected_kg==0x1p-53,"exact coalescing failed to release one supported ULP");
    MAOniColonyReceipt second{};require(copy_receipts(&second,1)==1 && receipt_job(second.sequence)==2,"coalesced source lost its own receipt");
    ack_receipts(second.sequence);stop();
    // Liquids retain the original conservative refusal. Passive-carrying gas
    // refusal is also covered by tiny_injection_neighbour_control's all-.6 case.
    const std::array liquid{material("MadeUpLiquid",112,water,false,.018,4200,1000)};
    for(auto& cell:cells)cell={0,MA_ONI_COLONY_SOLID,0,0,0,0};
    cells[12]={water,MA_ONI_COLONY_FLUID,1000,300,0,0};
    start(cfg,liquid,cells,1);enable_precise_receipts(true);auto liquid_request=injection;liquid_request.native_index=water;
    (void)queue_pipe_job(1,liquid_request,{});advance(1,.000001,2);wait();
    require(status().state==1 && status().injected_kg==0 && decode_precision_inventory(precision_inventory()).empty(),"unsupported liquid entered precision inventory");
    require(copy_receipts(&second,1)==1 && second.mass_kg==0,"unsupported liquid did not retain zero receipt");
    ack_receipts(second.sequence);require(checkpoint()[8]==12,"unsupported request changed owner framing");stop();
    for(auto& cell:cells)cell={oxygen,MA_ONI_COLONY_FLUID,.6f,300,0,0};
    cells[12]={0,MA_ONI_COLONY_SOLID,0,0,0,0};
    for(const double invalid_temperature:{.5,1e10}) {
        start(cfg,mats,cells,1);enable_precise_receipts(true);auto request=injection;request.temperature_k=invalid_temperature;
        (void)queue_pipe_job(1,request,{});advance(1,.000001,2);wait();
        require(status().state==1 && status().injected_kg==0 && decode_precision_inventory(precision_inventory()).empty(),"out-of-EOS source temperature entered precision inventory");
        require(copy_receipts(&second,1)==1 && second.mass_kg==0,"invalid-temperature source lost conservative refusal");
        ack_receipts(second.sequence);require(checkpoint()[8]==12,"invalid-temperature source changed owner framing");stop();
    }
    // The writer and reader share field/reporting validation. Construct raw
    // malformed records independently rather than asking the writer to emit
    // records its own admission contract forbids.
    for(unsigned mode=0;mode<5;++mode) {
        PrecisionParcel invalid{12,11,2,oxygen,0x1p-54,300,{}};invalid.energy_j.add(1);
        if(mode==0)invalid.radius=3;
        if(mode==1)invalid.mass_kg=-1;
        if(mode==2)invalid.temperature_k=0;
        if(mode==3){invalid.mass_kg=0;invalid.energy_j={};}
        if(mode==4){invalid.energy_j={};invalid.energy_j.add(std::numeric_limits<double>::max());invalid.energy_j.add(std::numeric_limits<double>::max());}
        bool rejected=false;try{(void)encode_precision_inventory(std::span(&invalid,1));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"precision producer accepted a decoder-invalid record");
        ParcelWireWriter raw;raw.raw("MAPREC01");raw.u32(1);raw.u32(invalid.source);raw.u32(invalid.anchor);raw.u32(invalid.radius);raw.u32(invalid.native_index);
        raw.f64(invalid.mass_kg);raw.f64(invalid.temperature_k);raw.sum(invalid.energy_j);
        rejected=false;try{(void)decode_precision_inventory(raw.bytes);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"precision reader accepted an encoder-invalid record");
    }
    // Independent exact before/after operands, not the implementation's
    // reported credit or the owner's millionth-scale audit. Both rounding
    // signs and a partial mass with a much larger energy correction.
    for(const double temperature:{300.,400.}) for(const bool partial:{false,true}) {
        ma::SpeciesRegistry registry;ma::SpeciesDefinition gas;gas.id="precision-control";gas.name=gas.id;
        gas.phase=ma::Phase::gas;gas.atoms={{"O",2}};gas.explicit_molar_mass=true;gas.molar_mass=.032;gas.cp=1000+ma::gas_constant/.032;
        gas.formation_enthalpy=gas.cp*ma::reference_temperature*gas.molar_mass;registry.add(gas);
        ma::experimental::ReferenceMaterial m;m.id=gas.id;m.phase=ReferencePhase::ideal_gas;
        m.molar_mass_kg_per_mol=.032;m.cv_j_per_kg_k=gas.cv();m.energy_offset_j_per_kg=gas.internal_energy(0);
        m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;m.provenance="Explicit precision inventory control";
        auto eos=std::make_shared<const ma::FluidThermodynamics>(registry,"precision-control-v1",std::vector{m});
        MacTransportState state;state.grid={1,1,1,1,false,false,{0}};state.cells.resize(1,MacMaterialCell{ma::MixtureInventory(1),{}});
        state.cells[0].mixture.masses[0]=.25;state.momentum_x.resize(2);state.momentum_y.resize(2);
        MacClosedSettings settings;settings.numerical_kernel=MacNumericalKernel::game;settings.game_profile=MacGameProfile::game_kernel;
        settings.vacuum=MacVacuumRepresentation::void_phase;
        auto world=MacClosedWorld::game_colony(registry,eos,std::move(state),std::span(&temperature,1),settings);
        const auto original=world;PrecisionParcel parcel{0,0,2,0,partial?0x1.00000001p-54:0x1p-54,temperature,{}};
        parcel.energy_j.add(parcel.mass_kg*(m.energy_offset_j_per_kg+m.cv_j_per_kg_k*temperature));const auto original_parcel=parcel;
        const std::array<std::size_t,1> destination{0};
        require(release_precision_parcel(world,parcel,0,0,destination),"controlled representable precision release refused");
        MacEnergySum mass;mass.add(original.state().cells[0].mixture.masses[0]);mass.add(original_parcel.mass_kg);
        mass.add(-world.state().cells[0].mixture.masses[0]);mass.add(-parcel.mass_kg);
        MacEnergySum energy=original_parcel.energy_j;energy.add(original.state().cells[0].mixture.energy);
        energy.add(-world.state().cells[0].mixture.energy);add_precision_sum(energy,parcel.energy_j,-1);
        require(mass.sign()==0 && energy.sign()==0,"independent exact held+grid mass/energy closure failed");
        require(parcel.energy_j.sign()==(temperature==300?1:-1),"controlled primary rounding sign was not exercised");
        const auto encoded=encode_precision_inventory(std::span(&parcel,1));
        require(encode_precision_inventory(decode_precision_inventory(encoded))==encoded,"signed precision correction codec changed operands");
        if(partial) {
            require(parcel.mass_kg>0 && parcel.mass_kg<original_parcel.mass_kg*0x1p-24,"controlled partial mass not exercised");
            const MacFluidTransfer empty{0,0,false,1,0};world=world.with_fluid_transfers(std::span(&empty,1),MacFluidTransferTopology::occupied_mixture).world;
            require(release_precision_parcel(world,parcel,0,0,destination) && parcel.mass_kg==0 &&
                std::abs(world.cell_reading(0).temperature_k-temperature)<1e-10,"signed correction corrupted residual material temperature");
        } else {
            require(parcel.mass_kg==0,"full precision release retained mass");
            // Make a finer U recipient without consuming the held correction.
            const MacFluidTransfer empty{0,0,false,1,0};world=world.with_fluid_transfers(std::span(&empty,1),MacFluidTransferTopology::occupied_mixture).world;
            const MacFluidTransfer fill{0,0,true,0x1p-20,temperature};world=world.with_fluid_transfers(std::span(&fill,1),MacFluidTransferTopology::occupied_mixture).world;
            const auto before=world.state().cells[0].mixture.energy;const auto correction=parcel.energy_j;
            require(release_precision_parcel(world,parcel,0,0,destination),"representable signed energy correction never releases");
            MacEnergySum balance=correction;balance.add(before);balance.add(-world.state().cells[0].mixture.energy);add_precision_sum(balance,parcel.energy_j,-1);
            require(balance.sign()==0,"heat-only correction created or discarded actual grid energy");
        }
    }
    std::cout<<"PASS precision inventory: actual native sub-ULP ownership, exact receipt energy, wrapper12 cold continuation, original radius and internal late release\n";
}
void pipe_job_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,3,103};
    start(cfg,mats,cells,1); const auto empty8=checkpoint();
    // A genuine version7 framing: same synthetic fields, no version8 suffix.
    auto legacy7=empty8; constexpr char physical_magic[]="MACCLS01";
    const auto physical=std::search(legacy7.begin(),legacy7.end(),physical_magic,physical_magic+8);
    require(physical!=legacy7.end() && physical-legacy7.begin()>20,"cannot locate synthetic physical image");
    const auto position=physical-legacy7.begin();
    require(std::all_of(legacy7.begin()+position-20,legacy7.begin()+position-8,[](auto b){return b==0;}),"unexpected empty job suffix");
    legacy7.erase(legacy7.begin()+position-20,legacy7.begin()+position-8); legacy7[8]=7;
    const auto digest=ma::oni::hash_memory(legacy7.data(),legacy7.size()-64);
    std::copy(digest.begin(),digest.end(),legacy7.end()-64);
    stop(); restore(cfg,mats,legacy7,1); require(checkpoint()==empty8,"legacy7 restore changed physical profile/state");
    enable_precise_receipts(true);
    const MAOniColonyTransfer legacy{4,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.02,300,0};
    queue_transfers(std::span(&legacy,1));
    const MAOniColonyTransfer injection{4,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.1234567890123,311.1234567890123,0};
    const std::array<MAOniColonyPassive,2> passives{{{3,.125},{7,.3333333333333333}}};
    require(queue_pipe_job(1,injection,passives)==std::pair<std::uint32_t,std::uint64_t>{1,0},"pipe token admission");
    const auto pending=checkpoint(); require(pipe_job_state(1).first==1,"save consumed pending pipe job");
    bool rejected=false; try { enable_precise_receipts(false); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"disabled precise delivery while pipe job was queued");
    const auto injection_outcome=[&] {
        require(queue_pipe_job(1,injection,passives).first==1,"duplicate pending token was requeued");
        auto different=injection; different.mass_kg=std::nextafter(different.mass_kg,1.);
        bool refused=false; try { (void)queue_pipe_job(1,different,passives); } catch (const std::invalid_argument&) { refused=true; }
        require(refused,"same pipe token accepted changed input");
        refused=false; try { (void)queue_pipe_job(3,injection,passives); } catch (const std::invalid_argument&) { refused=true; }
        require(refused && pipe_job_state(3).first==0,"pipe token gap admitted");
        advance(1,.2,2); wait(); require(status().state==1,"pipe injection fault");
        MAOniColonyReceipt receipt{}; require(copy_receipts(&receipt,1)==1 && receipt_job(receipt.sequence)==1,"durable token receipt association");
        require(pipe_job_state(1)==std::pair<std::uint32_t,std::uint64_t>{2,receipt.sequence},"pipe completion boundary");
        refused=false; try { (void)checkpoint(); } catch (const std::invalid_argument&) { refused=true; }
        require(refused,"saved uncredited committed pipe job");
        std::vector<std::uint8_t> wire(copy_precise_receipt(receipt.sequence,nullptr,0));
        copy_precise_receipt(receipt.sequence,wire.data(),static_cast<std::uint32_t>(wire.size()));
        ReceiptReader r{wire}; require(r.header(receipt.sequence,1)==1,"pipe injection parcel count");
        require(r.integer(4)==carbon && r.integer(4)==4 && r.integer(4)==1,"pipe injection indices");
        require(r.f64()==injection.mass_kg && r.f64()==injection.temperature_k,"pipe injection precision"); (void)r.f64();
        for (int i=0;i<4;++i) (void)r.scaled();
        require(r.integer(4)==2,"fractional/multiple pipe passives omitted");
        for (const auto& passive : passives) require(r.integer(8)==passive.identity && r.f64()==passive.quantity,"pipe injection passives rounded");
        require(r.observations && r.integer(4)==2 && r.integer(8)==3 && r.f64()==103 && r.f64()==103.125 &&
            r.integer(8)==7 && r.f64()==0 && r.f64()==passives[1].quantity,"pipe injection passive before/after metadata");
        require(r.at==wire.size(),"unexpected pipe parcel bytes");
        ack_receipts(receipt.sequence); require(pipe_job_state(1).first==3 && receipt_job(receipt.sequence)==0,"pipe outcome not retired");
        return wire;
    };
    const auto expected_wire=injection_outcome(); const auto expected=checkpoint();
    stop(); restore(cfg,mats,pending,1);
    require(pipe_job_state(1).first==1 && injection_outcome()==expected_wire && checkpoint()==expected,"queued pipe injection cold continuation differs");
    const MAOniColonyTransfer extraction{4,vacuum,MA_ONI_COLONY_EXTRACT_GAS,0,-1,0,.33333333333333,0,0};
    queue_pipe_job(2,extraction,{}); const auto pending_extraction=checkpoint();
    const auto extraction_outcome=[&] {
        advance(2,.2,2); wait(); require(status().state==1,"pipe extraction fault");
        std::array<MAOniColonyReceipt,2> receipts{}; require(copy_receipts(receipts.data(),2)==2,"mixture pipe extraction missed constituent outcome");
        const auto last=pipe_job_state(2); require(last.first==2 && last.second==receipts[1].sequence,"pipe last outcome sequence");
        std::vector<std::vector<std::uint8_t>> wires;
        for (const auto& receipt : receipts) {
            require(receipt_job(receipt.sequence)==2,"extraction token lost across load");
            std::vector<std::uint8_t> wire(copy_precise_receipt(receipt.sequence,nullptr,0));
            copy_precise_receipt(receipt.sequence,wire.data(),static_cast<std::uint32_t>(wire.size()));
            // Legacy receipt sequences intentionally restart after restore.
            std::fill(wire.begin()+8,wire.begin()+16,0); wires.push_back(std::move(wire));
        }
        ack_receipts(receipts[0].sequence); require(pipe_job_state(2)==last,"partial acknowledgement prematurely retired pipe job");
        bool refused=false; try { (void)checkpoint(); } catch (const std::invalid_argument&) { refused=true; }
        require(refused,"partial pipe outcome save admitted");
        ack_receipts(receipts[1].sequence); require(pipe_job_state(2).first==3,"full pipe outcome not retired");
        return wires;
    };
    const auto expected_extraction=extraction_outcome(); const auto expected_end=checkpoint();
    stop(); restore(cfg,mats,pending_extraction,1);
    require(pipe_job_state(2).first==1 && extraction_outcome()==expected_extraction && checkpoint()==expected_end,"queued extraction dropped or changed across restore");
    require(queue_pipe_job(2,extraction,{}).first==3,"retired pipe token requeued");
    auto zero=extraction; zero.mass_kg=0; queue_pipe_job(3,zero,{}); advance(3,.2,2); wait();
    MAOniColonyReceipt nothing{}; require(copy_receipts(&nothing,1)==1 && receipt_job(nothing.sequence)==3 &&
        copy_precise_receipt(nothing.sequence,nullptr,0)==28,"zero extraction lacks durable outcome");
    ack_receipts(nothing.sequence); enable_precise_receipts(false); stop();
    std::cout<<"PASS durable pipe jobs: full fractional passives, idempotence, token grouping, queued injection/extraction cold continuation, legacy7 and save barriers\n";
}
void actor_batch_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,3,103};
    start(cfg,mats,cells,1); enable_precise_receipts(true); const auto initial=checkpoint();
    std::array<MAOniColonyTransfer,5> edits{};
    for (auto& e : edits) { e.public_cell=4; e.kind=MA_ONI_COLONY_INJECT; e.native_index=oxygen; e.temperature_k=300; e.mass_kg=.02; e.callback=71; }
    edits[1].native_index=carbon; edits[1].callback=-1;
    edits[2].kind=MA_ONI_COLONY_EXTRACT; edits[2].native_index=carbon; edits[2].callback=72; edits[2].mass_kg=.009;
    edits[3].kind=MA_ONI_COLONY_EXTRACT_GAS; edits[3].callback=-1; edits[3].mass_kg=.015;
    edits[4].callback=73;
    const std::array<MAOniColonyPassive,2> passives{{{1,.125},{0x100000000ULL,std::ldexp(1.,-500)}}};
    std::array<MAOniColonyActorJob,2> jobs{{{1,0,2,0,1},{3,2,0,0,2}}};
    queue_transfers(std::span(edits).first(1)); const auto ordinary_pending=checkpoint();
    (void)queue_pipe_job(1,edits[1],passives);
    queue_transfers(std::span(edits).subspan(2,1));
    (void)queue_pipe_job(2,edits[3],{});
    queue_transfers(std::span(edits).last(1)); const auto ordered_pending=checkpoint();
    const auto outcomes=[&](bool live) {
        advance(1,.2,2); wait(); require(status().state==1,"actor batch advance faulted");
        std::vector<MAOniColonyReceipt> receipts(32); const auto count=copy_receipts(receipts.data(),32);
        require(count>=(live?5u:2u) && count<=32,"actor batch lost an ordered outcome"); receipts.resize(count);
        if (live) require(receipts.front().callback==71 && receipts.back().callback==73,"actor batch reordered ordinary actors");
        std::vector<std::vector<std::uint8_t>> sidecars;
        for (const auto& r : receipts) {
            const auto bytes=copy_precise_receipt(r.sequence,nullptr,0); sidecars.emplace_back(bytes);
            require(bytes && copy_precise_receipt(r.sequence,sidecars.back().data(),bytes)==bytes,"actor batch lost precise outcome");
        }
        require(pipe_job_state(1).first==2 && pipe_job_state(2).first==2,"actor jobs did not commit together");
        ack_receipts(receipts.back().sequence);
        return std::pair{receipts,sidecars};
    };
    const auto same=[](const auto& a,const auto& b) {
        return a.first.size()==b.first.size() &&
            std::memcmp(a.first.data(),b.first.data(),a.first.size()*sizeof(MAOniColonyReceipt))==0 && a.second==b.second;
    };
    const auto baseline=outcomes(true); const auto completed=checkpoint(); stop();
    restore(cfg,mats,initial,1); enable_precise_receipts(true);
    const auto rejects=[&](auto&& call) {
        const auto before=checkpoint(); bool rejected=false;
        try { call(); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && checkpoint()==before,"rejected actor batch partially changed saved inputs");
    };
    auto invalid_jobs=jobs; invalid_jobs[1].token=4;
    rejects([&] { queue_actor_batch(edits,invalid_jobs,passives); });
    invalid_jobs=jobs; invalid_jobs[1].transfer_index=1;
    rejects([&] { queue_actor_batch(edits,invalid_jobs,passives); });
    invalid_jobs=jobs; invalid_jobs[1].reserved=1;
    rejects([&] { queue_actor_batch(edits,invalid_jobs,passives); });
    invalid_jobs=jobs; invalid_jobs[1].first_passive=1;
    rejects([&] { queue_actor_batch(edits,invalid_jobs,passives); });
    auto invalid_passives=passives; invalid_passives[1].identity=1;
    rejects([&] { queue_actor_batch(edits,jobs,invalid_passives); });
    auto invalid_edits=edits; invalid_edits.back().mass_kg=-1;
    rejects([&] { queue_actor_batch(invalid_edits,jobs,passives); });
    require(pipe_job_state(1).first==0 && pipe_job_state(2).first==0,"invalid actor batch accepted a token prefix");
    queue_actor_batch(edits,jobs,passives);
    require(checkpoint()==ordered_pending,"atomic actor batch differs from ordered single enqueues before advance");
    rejects([&] { queue_actor_batch(edits,jobs,passives); });
    const auto queued=checkpoint();
    require(same(outcomes(true),baseline) && checkpoint()==completed,"atomic actor live order/physics differs from single enqueues");
    stop(); restore(cfg,mats,ordered_pending,1);
    // Owner8 deliberately strips transient callback handles and drops their
    // extractions. Compare both cold paths under that unchanged save policy.
    const auto cold_baseline=outcomes(false); const auto cold_completed=checkpoint();
    stop(); restore(cfg,mats,queued,1);
    require(checkpoint()==queued,"cold actor batch did not preserve pending order/metadata");
    rejects([&] { queue_actor_batch(edits,jobs,passives); });
    const auto resumed=outcomes(false);
    require(same(resumed,cold_baseline) && checkpoint()==cold_completed,"actor batch cold continuation differs in physics, receipts, passives or owner bytes");
    stop(); restore(cfg,mats,initial,1); enable_precise_receipts(false);
    rejects([&] { queue_actor_batch(edits,jobs,passives); });
    enable_precise_receipts(true); queue_actor_batch(std::span(edits).first(1),{},{});
    require(checkpoint()==ordinary_pending,"ordinary-only actor batch changes legacy enqueue bytes"); stop();
    start(cfg,mats,cells,1); enable_precise_receipts(true);
    std::exception_ptr producer_error;
    std::thread producer([&] {
        try {
            for (std::uint64_t n=0;n<50;++n) {
                std::array<MAOniColonyTransfer,3> batch{};
                for (auto& e : batch) { e.public_cell=4; e.kind=MA_ONI_COLONY_INJECT; e.native_index=oxygen; e.temperature_k=300; e.mass_kg=.0001; }
                batch[0].callback=static_cast<std::int32_t>(2000+2*n);
                batch[1].callback=-1; batch[1].native_index=carbon;
                batch[2].callback=static_cast<std::int32_t>(2001+2*n);
                const MAOniColonyActorJob descriptor{1,0,0,0,n+1};
                queue_actor_batch(batch,std::span(&descriptor,1),{});
            }
        } catch (...) { producer_error=std::current_exception(); }
    });
    for (std::uint64_t ticket=1;ticket<=50;++ticket) advance(ticket,.01,2);
    producer.join(); if (producer_error) std::rethrow_exception(producer_error);
    advance(51,.01,2); wait(); require(status().state==1,"concurrent actor batches faulted");
    std::array<MAOniColonyReceipt,256> concurrent{};
    const auto count=copy_receipts(concurrent.data(),256); require(count==150,"concurrent actor batches lost or duplicated requests");
    std::array<std::uint64_t,50> frames{}; frames.fill(UINT64_MAX); std::array<unsigned,50> counts{};
    for (std::size_t i=0;i<count;++i) {
        const auto& r=concurrent[i]; std::uint64_t n=0;
        if (r.callback>=2000 && r.callback<2100) n=static_cast<std::uint64_t>(r.callback-2000)/2;
        else { const auto token=receipt_job(r.sequence); require(token>=1 && token<=50,"concurrent actor batch lost job identity"); n=token-1; }
        if (frames[n]==UINT64_MAX) frames[n]=r.completed_frame;
        require(frames[n]==r.completed_frame,"one actor batch crossed the worker input-drain boundary"); ++counts[n];
    }
    require(std::all_of(counts.begin(),counts.end(),[](auto n){return n==3;}),"concurrent actor batch grouped wrong actors");
    ack_receipts(concurrent[count-1].sequence); stop();
    std::cout<<"PASS atomic actor batch: exact order, all-or-nothing invalid/replay admission, full passives and owner8 cold continuation\n";
}
// Owner wrapper 13 (docs/colony-owner.md#saves): callback-owned requests in
// flight at a save keep their queue place under ordinals and are rebound to
// this session's callbacks after a restore. Bound, the restored owner
// continues byte for byte with the uninterrupted one; unbound requests resolve
// exactly as an older save did.
void callback_rebind_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[3]={oxygen,MA_ONI_COLONY_FLUID,1.1f,295,3,103}; cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,3,103}; cells[5]={carbon,MA_ONI_COLONY_FLUID,.7f,290,3,103};
    constexpr std::int32_t handle=0x7e000005, new_handle=0x1a00002b; // a native handle in the restored session
    std::array<MAOniColonyTransfer,6> edits{};
    for (auto& e : edits) { e.public_cell=4; e.kind=MA_ONI_COLONY_INJECT; e.native_index=oxygen; e.temperature_k=300; e.mass_kg=.02; e.callback=-1; }
    edits[0].callback=71;                                                            // answered injection (an emitter callback)
    edits[1].native_index=carbon;                                                    // unanswered injection (a native change)
    edits[2].kind=MA_ONI_COLONY_EXTRACT; edits[2].native_index=oxygen; edits[2].callback=72; edits[2].mass_kg=.009; edits[2].radius=1; // a breath
    edits[3].kind=MA_ONI_COLONY_EXTRACT_GAS|MA_ONI_COLONY_CONSUMER; edits[3].callback=handle; edits[3].mass_kg=.015; edits[3].radius=1; // a consumer
    edits[4].kind=MA_ONI_COLONY_EXTRACT; edits[4].native_index=carbon; edits[4].mass_kg=.004; edits[4].public_cell=5;   // unanswered extraction
    edits[5].callback=73; edits[5].public_cell=3;
    start(cfg,mats,cells,1);
    {   // No callback-owned request: the base wrapper and its bytes are unchanged.
        queue_transfers(std::span(edits).subspan(1,1)); const auto plain=checkpoint();
        std::uint32_t version=0; std::memcpy(&version,plain.data()+8,4);
        require(version==8 && checkpoint_callbacks().empty(),"unanswered queue changed its owner wrapper");
        stop(); start(cfg,mats,cells,1);
    }
    queue_transfers(edits); const auto pending=checkpoint();
    std::array<std::uint32_t,3> head{}; std::memcpy(head.data(),pending.data()+8,12);
    require(head[0]==13 && head[1]==8 && head[2]==4,"callback-owned requests were not framed by owner wrapper 13");
    const auto written=checkpoint_callbacks();
    require(written.size()==4 && written[0].callback==71 && written[1].callback==72 && written[2].callback==handle && written[3].callback==73 &&
        written[2].kind==(MA_ONI_COLONY_EXTRACT_GAS|MA_ONI_COLONY_CONSUMER) && written[1].public_cell==4 && written[3].public_cell==3 &&
        std::all_of(written.begin(),written.end(),[](const auto& r){return r.awaiting==0 && r.struct_size==32 && r.abi_version==1;}),
        "checkpoint misreported its callback-owned requests");
    const auto frame=[&] {
        advance(1,.2,2); wait(); require(status().state==1,"callback rebind frame faulted");
        std::vector<MAOniColonyReceipt> receipts(64); receipts.resize(copy_receipts(receipts.data(),64));
        if (!receipts.empty()) ack_receipts(receipts.back().sequence);
        for (auto& r : receipts) r.sequence=0; // sequences restart after a restore
        return receipts;
    };
    const auto live=frame(); const auto completed=checkpoint();
    const auto mapped=[&](std::vector<MAOniColonyReceipt> receipts,const std::map<std::int32_t,std::int32_t>& callbacks) {
        for (auto& r : receipts) if (const auto c=callbacks.find(r.callback); c!=callbacks.end()) r.callback=c->second;
        return receipts;
    };
    const auto same=[](const std::vector<MAOniColonyReceipt>& a,const std::vector<MAOniColonyReceipt>& b) {
        return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(MAOniColonyReceipt))==0;
    };
    // Restored: the queue is unchanged, the requests await their actors.
    stop(); restore(cfg,mats,pending,1);
    require(callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{4,0},"restored requests do not await rebinding");
    require(checkpoint()==pending,"saving before rebinding changed the restored queue");
    const auto awaiting=checkpoint_callbacks();
    require(awaiting.size()==4 && std::all_of(awaiting.begin(),awaiting.end(),[&](const auto& r){return r.awaiting==1 && r.callback==static_cast<std::int32_t>(&r-awaiting.data());}),
        "awaiting requests misreported");
    const auto rejects=[&](std::vector<MAOniColonyCallbackRebind> values) {
        bool rejected=false; try { rebind_callbacks(values); } catch (const std::invalid_argument&) { rejected=true; }
        require(rejected && callback_rebind_state().first==4,"invalid rebinding was admitted");
    };
    rejects({{171,1},{172,1},{new_handle,1}});
    rejects({{171,1},{-5,1},{new_handle,1},{173,1}});
    rejects({{171,2},{172,1},{new_handle,1},{173,1}});
    rebind_callbacks(std::vector<MAOniColonyCallbackRebind>{{171,1},{172,1},{new_handle,1},{173,1}});
    require(callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{0,0},"bound requests still await");
    const auto bound=frame();
    require(same(mapped(live,{{71,171},{72,172},{handle,new_handle},{73,173}}),bound) && checkpoint()==completed,
        "a rebound restored owner differs from the uninterrupted owner");
    // Released requests behave exactly as the previous save policy: the
    // answered injection stays unanswered and the extractions are withdrawn.
    stop(); start(cfg,mats,cells,1);
    auto legacy=edits; legacy[0].callback=-1; legacy[5].callback=-1;
    queue_transfers(std::array{legacy[0],legacy[1],legacy[4],legacy[5]});
    const auto legacy_receipts=frame(); const auto legacy_completed=checkpoint();
    stop(); restore(cfg,mats,pending,1);
    const auto released=frame();
    require(callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{0,4} && same(released,legacy_receipts) && checkpoint()==legacy_completed,
        "unbound requests differ from the previous save policy");
    // A partial release withdraws only the released extraction.
    stop(); restore(cfg,mats,pending,1);
    rebind_callbacks(std::vector<MAOniColonyCallbackRebind>{{171,1},{0,0},{new_handle,1},{173,1}});
    require(callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{0,1},"partial release miscounted");
    const auto partial=frame();
    require(std::none_of(partial.begin(),partial.end(),[](const auto& r){return r.callback==72 || r.callback==0;}) &&
        std::count_if(partial.begin(),partial.end(),[&](const auto& r){return r.callback==new_handle;})>=1,"partial release kept or misrouted a request");
    stop();
    // Corrupted ordinals or counts refuse the save.
    const auto refuses=[&](std::vector<std::uint8_t> image,const char* why) {
        const auto body=image.size()-64; const auto digest=ma::oni::hash_memory(image.data(),body); std::copy(digest.begin(),digest.end(),image.begin()+body);
        bool refused=false; try { restore(cfg,mats,image,1); } catch (const std::invalid_argument&) { refused=true; }
        if (!refused) stop();
        require(refused,why);
    };
    auto wrong_count=pending; wrong_count[16]=5; refuses(wrong_count,"wrong callback-owned request count restored");
    // The breath (ordinal 1) is the queue's third transfer: give it ordinal 2.
    const std::array<std::uint32_t,5> breath{4,oxygen,MA_ONI_COLONY_EXTRACT,1,1}; std::array<std::uint8_t,20> pattern{}; std::memcpy(pattern.data(),breath.data(),20);
    auto wrong_ordinal=pending; const auto at=std::search(wrong_ordinal.begin(),wrong_ordinal.end(),pattern.begin(),pattern.end());
    require(at!=wrong_ordinal.end(),"saved breath request not found"); *(at+16)=2; refuses(wrong_ordinal,"out-of-order callback ordinal restored");
    std::cout<<"PASS callback-owned requests: wrapper13 framing, awaiting saves, exact rebound continuation, release as the previous policy, refusal of corrupt ordinals\n";
}
void sample_history_control() {
    using namespace ma::oni::colony;
    {
        using namespace ma; using namespace ma::experimental;
        // Opposed formation energies cancel. Each cell carries a small U
        // remainder that disappears if primary U is projected before summing.
        SpeciesRegistry registry; std::vector<ReferenceMaterial> definitions;
        for (int sign : {1,-1}) {
            SpeciesDefinition gas;gas.id=sign>0?"sample-positive":"sample-negative";
            gas.name=gas.id;gas.phase=Phase::gas;gas.atoms={{"O",2}};gas.explicit_molar_mass=true;
            gas.molar_mass=.032;gas.cp=1000+gas_constant/gas.molar_mass;
            gas.formation_enthalpy=(sign*std::ldexp(1.,60)+gas.cp*reference_temperature)*gas.molar_mass;
            registry.add(gas);ReferenceMaterial m;m.id=gas.id;m.phase=ReferencePhase::ideal_gas;
            m.molar_mass_kg_per_mol=gas.molar_mass;m.cv_j_per_kg_k=gas.cv();m.energy_offset_j_per_kg=gas.internal_energy(0);
            m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;m.provenance="Synthetic energy observation control";
            definitions.push_back(m);
        }
        auto eos=std::make_shared<const FluidThermodynamics>(registry,"energy-observation-v1",definitions);
        MacTransportState state;state.grid={2,1,1,1,false,false,std::vector<std::uint8_t>(2)};
        state.cells.resize(2,MacMaterialCell{MixtureInventory(2),{}});
        long double expected=0;
        for (std::size_t i=0;i<2;++i) {
            auto& cell=state.cells[i];cell.mixture.masses[i]=1;
            cell.mixture.energy=definitions[i].energy_offset_j_per_kg+300000;
            cell.energy_remainder_j.projected=.03125;
            expected+=static_cast<long double>(cell.mixture.energy);
        }
        expected+=.0625L; // Add the two known remainders after the large cancellation.
        state.momentum_x={0,3,0};state.momentum_y.resize(4);
        state.full_momentum_x={MacScaled{},MacScaled::from_double(3),MacScaled{}};
        const MacClosedWorld world(registry,eos,std::move(state));
        const auto observation=energy_components(world);
        require(observation.internal_energy_j==static_cast<double>(expected),"arena energy flattened U remainders");
        require(observation.kinetic_energy_j==4.5,"arena energy lost canonical momentum");
        require(observation.fluid_mass_kg==2 &&
            observation.heat_capacity_j_per_k==registry[0].cv()+registry[1].cv(),"arena energy changed stored mass/Cv");
        const std::array<std::size_t,2> selected{1,0};
        const auto regional=region_energy_components(world,selected);
        require(std::memcmp(&regional,&observation,sizeof(observation))==0,
            "regional observation flattened carried U/J or counted an internal face twice");
        bool rejected=false;const std::size_t first_cell=0;
        try{(void)region_energy_components(world,std::span(&first_cell,1));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"regional observation accepted an open boundary");
        auto blocked_state=world.state();blocked_state.grid.closed_x={0,1,0};
        const MacClosedWorld blocked_with_j(registry,eos,blocked_state);rejected=false;
        try{(void)region_energy_components(blocked_with_j,std::span(&first_cell,1));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"regional observation accepted boundary canonical momentum");
        blocked_state.momentum_x[1]=0;blocked_state.full_momentum_x[1]={};
        const MacClosedWorld blocked(registry,eos,std::move(blocked_state));
        require(region_energy_components(blocked,std::span(&first_cell,1)).fluid_mass_kg==1,
            "regional observation rejected an explicitly closed zero-J face");
    }
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0}; start(cfg,mats,cells,1);
    const auto initial=checkpoint(); sample_history(3); require(checkpoint()==initial,"history enabled changed checkpoint");
    const std::uint32_t cell=4; MAOniColonyStatus source{};
    std::vector<MAOniColonySampleCell> rows; std::vector<MAOniColonySamplePart> parts; double mass=-1,heat=-1;
    const auto read=[&](std::uint64_t frame) { sample_frame(frame,std::span(&cell,1),source,rows,parts,&mass,&heat); };
    read(0); const auto first=parts; require(source.completed_frames==0 && heat==0,"history initial frame/heat");
    const auto first_energy=sample_energy(source.session,0);
    require(first_energy.session==source.session && first_energy.completed_frame==0 &&
        first_energy.accepted_external_heat_j==0 && first_energy.kinetic_energy_j==0,
        "initial arena energy epoch/heat");
    const MAOniColonyHeat warm{4,0,25.25,1000}; queue_heat(std::span(&warm,1)); advance(1,.2,2); wait();
    const MAOniColonyHeat cool{4,0,-1,1000}; queue_heat(std::span(&cool,1)); advance(2,.2,2); wait();
    require(status().state==1,"history heat control faulted"); const auto at2=checkpoint();
    read(0); require(heat==0 && source.completed_frames==0 && parts.size()==first.size() &&
        std::memcmp(parts.data(),first.data(),first.size()*sizeof(MAOniColonySamplePart))==0,"old history frame mutated");
    read(1); require(heat==25.25 && source.completed_frames==1,"history lost accepted external heat epoch");
    const auto warm_energy=sample_energy(source.session,1);
    require(warm_energy.completed_frame==1 && warm_energy.time_seconds==source.time_seconds &&
        warm_energy.accepted_external_heat_j==25.25 &&
        std::abs(warm_energy.internal_energy_j-first_energy.internal_energy_j-25.25)<1e-8,
        "retained energy/accepted heat came from different frames");
    read(2); require(heat==24.25 && source.completed_frames==2 && checkpoint()==at2,"history heat/read changed physical state");
    advance(3,.2,2); wait();
    const auto before=checkpoint(); bool rejected=false;
    try { read(0); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected && checkpoint()==before,"evicted history silently returned another world");
    rejected=false;try{(void)sample_energy(first_energy.session,0);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"evicted arena energy silently returned a later frame");
    read(UINT64_MAX); require(source.completed_frames==3 && heat==24.25,"latest history selector");
    const MAOniColonyHeat overcap{4,0,1e12,301}; queue_heat(std::span(&overcap,1)); advance(4,.2,2); wait();
    read(4); require(heat==24.25 && status().state==1,"queued refused heat was counted as accepted");
    sample_history(0); rejected=false; try { read(4); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"disabled history retained old samples"); stop(); start(cfg,mats,cells,1);
    rejected=false; try { read(1); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"new session exposed prior history");
    sample_history(1);rejected=false;try{(void)sample_energy(first_energy.session,0);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"arena energy crossed a session boundary");stop();
    std::cout<<"PASS retained arena samples: exact completed frame, accepted/deferred heat, bounded eviction, readonly checkpoint and session reset\n";
}
void native_sample_history_control() {
    using namespace ma::oni::colony;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0};start(cfg,mats,cells,1);sample_history(8);
    const auto session_id=status().session;
    std::array<std::uint16_t,25> elements[2];std::array<float,25> masses[2],temperatures[2];
    NativeCells native;native.pitch=native.rows=5;
    for(unsigned b=0;b<2;++b) {
        elements[b].fill(99);masses[b].fill(42);temperatures[b].fill(318.25f);
        native.element[b]=elements[b].data();native.mass[b]=masses[b].data();native.temperature[b]=temperatures[b].data();
        elements[b][12]=oxygen;
    }
    const std::array<std::uint32_t,2> requested{1,4};
    MAOniColonyStatus source{};std::vector<MAOniColonyNativeSampleCell> rows;
    const auto read=[&](std::uint64_t frame){sample_native_frame(session_id,frame,requested,source,rows);};
    bool refused=false;try{read(0);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"initial world invented a native boundary source");
    const auto capture=[&](std::uint64_t ticket,float solid_temperature) {
        write_shadow(native);temperatures[1][7]=solid_temperature;temperatures[1][12]+=.125f;
        capture_shadow(native,ticket);
    };
    capture(1,319.25f);advance(1,.2,2);wait();require(status().state==1,"native sample fixture faulted");
    const auto first_checkpoint=checkpoint();read(1);
    require(source.native_ticket==1 && source.completed_frames==1 && rows.size()==2 &&
        rows[0].public_cell==1 && rows[0].native_index==99 && rows[0].mass_kg==42 && rows[0].temperature_k==319.25,
        "native solid endpoint lost its source ticket or values");
    const auto first=rows;
    const auto energy=sample_energy(session_id,1);
    require(energy.accepted_external_heat_j==static_cast<double>(1.2f)*1000*.125 && checkpoint()==first_checkpoint,
        "native sample epoch differs from accepted heat or observation changed checkpoint");
    capture(2,320.5f);advance(2,.2,2);wait();read(1);
    require(std::memcmp(first.data(),rows.data(),rows.size()*sizeof(rows[0]))==0,"native retained solid temperatures followed live arrays");
    read(2);require(source.native_ticket==2 && rows[0].temperature_k==320.5,"native solid frame did not advance");
    const auto rejects=[&](std::uint64_t frame) {
        const auto before=checkpoint();const auto held_source=source;const auto held_rows=rows;
        bool failed=false;try{read(frame);}catch(const std::invalid_argument&){failed=true;}
        require(failed && std::memcmp(&source,&held_source,sizeof(source))==0 && rows.size()==held_rows.size() &&
            std::memcmp(rows.data(),held_rows.data(),rows.size()*sizeof(rows[0]))==0 && checkpoint()==before,
            "unpaired native sample wrote outputs or changed checkpoint");
    };
    capture(300,321);advance(3,.2,2);wait();rejects(3); // source ticket mismatch
    capture(4,322);capture(4,323);advance(4,.2,2);wait();rejects(4); // two native inputs
    // A missing diagnostic capture still books its physical heat, but cannot
    // be presented as the same native interval as the successful later one.
    const auto true_rows=native.rows;native.rows=0;capture(5,324);native.rows=true_rows;
    capture(5,325);advance(5,.2,2);wait();rejects(5);
    sample_history(0);rejects(2);stop();start(cfg,mats,cells,1);sample_history(1);rejects(0);stop();
    std::cout<<"PASS paired native arena samples: solid/heat ticket, immutable retained values, missing/coalesced rejection and readonly checkpoint\n";
}
void region_energy_control() {
    using namespace ma::oni::colony;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=256;cfg.grid_height=cfg.height=32;
    cfg.maximum_timestep_s=.1;cfg.gravity_m_per_s2=9.81;
    std::vector<MAOniColonyCell> cells(8192,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[257]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};cells[258]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    cells[510]={oxygen,MA_ONI_COLONY_FLUID,37,350,0,0};start(cfg,mats,cells,1);
    const auto s=status();const auto before=checkpoint();const std::array<std::uint32_t,2> requested{258,257};
    const auto e=sample_region_energy(s.session,s.completed_frames,requested);
    require(e.struct_size==88 && e.abi_version==1 && e.session==s.session && e.completed_frame==0 &&
        e.fluid_mass_kg==2 && std::abs(e.heat_capacity_j_per_k-2000)<1e-10 && std::abs(e.thermal_energy_j-600000)<1e-8 &&
        std::abs(e.potential_energy_j-29.43)<1e-12 && checkpoint()==before,
        "large owner region energy included another component or changed checkpoint");
    bool rejected=false;try{(void)sample_region_energy(s.session,0,std::span(requested.data(),1));}
    catch(const std::invalid_argument&){rejected=true;}require(rejected,"large owner region accepted an open cut");
    const std::array<std::uint32_t,2> duplicate{257,257};rejected=false;
    try{(void)sample_region_energy(s.session,0,duplicate);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"region energy accepted duplicate cells");
    for(const auto&[session_id,frame]:{std::pair{s.session+1,0ull},std::pair{s.session,1ull}}) {
        rejected=false;try{(void)sample_region_energy(session_id,frame,requested);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"region energy silently sampled another session/frame");
    }
    require(checkpoint()==before,"failed region observations changed checkpoint");stop();
    std::cout<<"PASS sealed region energy: large owner, component exclusion, canonical U/J, closed boundaries and exact immutable epoch\n";
}
void view_cache_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0}; start(cfg,mats,cells,1);
    std::array<MAOniColonyCellView,9> actual{},expected{}; MAOniColonyStatus current{},reference{};
    require(copy_changed(UINT64_MAX,UINT64_MAX,current,actual.data(),9),"initial view cache did not copy");
    copy(reference,expected.data(),9);
    require(std::memcmp(actual.data(),expected.data(),sizeof(actual))==0 && std::memcmp(&current,&reference,sizeof(current))==0,
        "initial cached view/status differs from complete copy");
    const auto session_id=current.session;
    std::array<MAOniColonyCellView,9> sentinel{}; std::memset(sentinel.data(),0x55,sizeof(sentinel)); const auto unchanged=sentinel;
    require(!copy_changed(session_id,0,current,sentinel.data(),9) && std::memcmp(sentinel.data(),unchanged.data(),sizeof(sentinel))==0,
        "same-frame cache wrote the destination");
    bool rejected=false; try { copy_changed(session_id,0,current,actual.data(),1); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"cache accepted an undersized unchanged buffer");
    std::array<std::uint16_t,25> elements[2]{}; std::array<float,25> masses[2]{},temperatures[2]{};
    NativeCells native; native.pitch=native.rows=5;
    for (int i=0;i<2;++i) { elements[i][12]=oxygen; native.element[i]=elements[i].data(); native.mass[i]=masses[i].data(); native.temperature[i]=temperatures[i].data(); }
    write_shadow(native); for (int i=0;i<2;++i) temperatures[i][12]+=1;
    capture_shadow(native); const auto before=checkpoint();
    require(!copy_changed(session_id,0,current,actual.data(),9) && current.native_changes>0 && current.native_heat_j>0,
        "same-frame cache hid live native exchanges");
    copy(reference,expected.data(),9);
    require(std::memcmp(&current,&reference,sizeof(current))==0 && std::memcmp(actual.data(),expected.data(),sizeof(actual))==0 && checkpoint()==before,
        "same-frame status/views differ or copy changed the physical checkpoint");
    advance(1,.2,2); wait(); require(status().state==1 && copy_changed(session_id,0,current,actual.data(),9),"new physical frame was cached");
    copy(reference,expected.data(),9); require(std::memcmp(actual.data(),expected.data(),sizeof(actual))==0,"new-frame view copy differs");
    const auto saved=checkpoint(); stop(); restore(cfg,mats,saved,1);
    require(copy_changed(session_id,1,current,actual.data(),9) && current.session!=session_id && current.completed_frames==1,
        "restored equal frame reused a prior session's views");
    copy(reference,expected.data(),9); require(std::memcmp(actual.data(),expected.data(),sizeof(actual))==0 && checkpoint()==saved,"restored cache changed continuation");
    stop();
    std::cout<<"PASS immutable view reuse: complete bytes/status, unchanged buffer, live exchanges, new frame, cold session and checkpoint\n";
}
void sample_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000),material("CarbonDioxide",12,carbon,true,.044,850)};
    MAOniColonyConfig cfg{}; cfg.struct_size=sizeof(cfg); cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum; cfg.grid_width=cfg.width=3; cfg.grid_height=cfg.height=3; cfg.maximum_timestep_s=.1;
    std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[4]={oxygen,MA_ONI_COLONY_FLUID,1.2f,300,0,0}; start(cfg,mats,cells,1);
    const MAOniColonyTransfer injection{4,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.1234567890123,311.1234567890123,0};
    refuse_reading(4); queue_transfers(std::span(&injection,1)); advance(1,.2,2); wait();
    require(status().state==1,"sample refused-reading fixture fault");
    std::vector<MAOniColonyReading> presentation; std::vector<MAOniColonyConstituent> visible; readings(presentation,visible);
    require(presentation.empty() && visible.empty(),"refused projection control did not omit presentation reading");
    const auto saved=checkpoint();
    const std::array<std::uint32_t,3> requested{4,0,8}; MAOniColonyStatus source{};
    std::vector<MAOniColonySampleCell> rows; std::vector<MAOniColonySamplePart> parts;
    sample(requested,source,rows,parts);
    require(source.completed_frames==1 && rows.size()==3 && rows[0].flags==3 && rows[0].temperature_k==300 && rows[0].constituents==2 &&
        rows[1].flags==0 && rows[1].constituents==0 && rows[2].flags==0,"sample lost ownership/refusal/order/projected temperature");
    double mass=0; for (const auto& part : parts) mass+=part.mass_kg;
    require(parts.size()==2 && parts[0].native_index==oxygen && parts[1].native_index==carbon &&
        parts[0].mass_kg==static_cast<double>(1.2f) && parts[1].mass_kg==injection.mass_kg && mass>1.3,"sample omitted refused cell constituent mass");
    bool rejected=false; const std::array<std::uint32_t,2> duplicate{4,4};
    try { sample(duplicate,source,rows,parts); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected && checkpoint()==saved,"sample admitted duplicate cells or mutated trajectory/checkpoint");
    refuse_reading(-1); stop();
    std::cout<<"PASS immutable full-composition arena sample, refused projections, source frame/audits and unchanged checkpoint\n";
}
void conversion_batch_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("SaltWater",15,brine,false,.021,4100,1100),
        material("Steam",14,steam,true,.018,4000)};
    mats[0].high_temperature_k=372.5; mats[0].high_target=14; mats[0].high_byproduct=777; mats[0].high_byproduct_fraction=.07;
    mats[1].low_target=13; // no paired water: this uses the identity conversion path
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=static_cast<double>(.1f);
    std::vector<MAOniColonyCellView> expected(9),views(9);
    std::vector<MAOniColonyReading> expected_readings;std::vector<MAOniColonyConstituent> expected_parts;
    for (const bool germs : {false,true}) {
        std::vector<MAOniColonyCell> cells(9,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
        cells[4]={brine,MA_ONI_COLONY_FLUID,1100,420,germs?3u:0u,germs?1000:0};
        start(cfg,mats,cells,1);advance(1,static_cast<double>(.2f),2);wait();
        MAOniColonyStatus st{};copy(st,views.data(),static_cast<std::uint32_t>(views.size()));
        const std::array<std::uint32_t,1> sampled_cells{4}; MAOniColonyStatus sampled_status{};
        std::vector<MAOniColonySampleCell> sampled_rows; std::vector<MAOniColonySamplePart> sampled_parts; double booked_mass=0;
        sample(sampled_cells,sampled_status,sampled_rows,sampled_parts,&booked_mass);
        double remaining=0; for (const auto& part : sampled_parts) remaining+=part.mass_kg;
        require(booked_mass<1100 && std::abs(remaining+sampled_status.reservoir_kg-booked_mass)<1e-8,
            "sample full booked inventory omitted solid-byproduct conversion flows");
        require(st.state==1 && st.converted_kg>1 && st.maximum_relative_mass_error<1e-12 && st.maximum_relative_energy_error<1e-12,
            "conversion batch control fault, missing conversion or open ledger");
        std::vector<MAOniColonyReading> current;std::vector<MAOniColonyConstituent> parts;readings(current,parts);
        if (germs) require(views[4].disease==3 && views[4].disease_count==1000,"conversion batch lost carried germs");
        for (auto& view : views) { view.disease=0;view.disease_count=0; }
        if (!germs) { expected=views;expected_readings=current;expected_parts=parts; }
        else require(std::memcmp(views.data(),expected.data(),views.size()*sizeof(views[0]))==0 &&
            current.size()==expected_readings.size() && parts.size()==expected_parts.size() &&
            std::memcmp(current.data(),expected_readings.data(),current.size()*sizeof(current[0]))==0 &&
            std::memcmp(parts.data(),expected_parts.data(),parts.size()*sizeof(parts[0]))==0,
            "sterile joined conversion differs from the original germ-carrying path");
        const auto image=checkpoint();advance(2,static_cast<double>(.2f),2);wait();const auto endpoint=checkpoint();
        stop();restore(cfg,mats,image,1);advance(2,static_cast<double>(.2f),2);wait();
        require(status().state==1 && checkpoint()==endpoint,"conversion batch cold continuation changed");stop();
    }
    std::cout<<"PASS sterile joined and germ-carrying conversions: view/composition bytes, ledgers and cold continuation\n";
}
void timestep_control() {
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.016,1000)};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=8;cfg.grid_height=cfg.height=4;
    cfg.maximum_timestep_s=static_cast<double>(.1f);
    std::vector<MAOniColonyCell> cells(32);
    for(std::size_t i=0;i<cells.size();++i)
        cells[i]={oxygen,MA_ONI_COLONY_FLUID,i%8<4?1.2f:std::nextafter(1.2f,2.f),300,0,0};
    ma::oni::colony::start(cfg,mats,cells,1);
    ma::oni::colony::advance(1,static_cast<double>(.2f),2);ma::oni::colony::wait();
    MAOniColonyStatus st{};ma::oni::colony::copy(st,nullptr,0);
    require(st.state==1 && st.completed_frames==1 && st.substeps==2 &&
        st.maximum_relative_mass_error<1e-12 && st.maximum_relative_energy_error<1e-12,
        "frame-aligned timestep must advance in two steps with closed ledgers");
    ma::oni::colony::stop();
    cfg.maximum_timestep_s=std::nextafter(cfg.maximum_timestep_s,1.);
    bool rejected=false;
    try{ma::oni::colony::start(cfg,mats,cells,1);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected && !ma::oni::colony::active(),"timestep beyond native frame-aligned cap must reject");
    std::cout<<"PASS native frame-aligned timestep: two steps, ledgers and admission cap\n";
}
void projection_control() {
    using namespace ma::experimental;
    constexpr std::uint32_t width = 64, height = 35;
    const auto saved = mac_kernel_default_options();
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .016, 1000),
        material("CarbonDioxide", 12, carbon, true, .044, 850), material("Water", 13, water, false, .018, 4000, 1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = width; cfg.grid_height = cfg.height = height;
    cfg.maximum_timestep_s = .01;
    std::vector<MAOniColonyCell> cells(width * height);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i % 13 == 0) cells[i] = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
        else if (i % 7 == 0) cells[i] = {vacuum, MA_ONI_COLONY_FLUID, 0, 0, 0, 0};
        else cells[i] = {i / width >= height - 3 ? water : i % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID,
            i / width >= height - 3 ? 1000.f : 1.2f, 300, 3, 100};
    }
    ma::oni::colony::start(cfg, mats, cells, 1);
    const auto source = ma::oni::colony::checkpoint();
    std::vector<MAOniColonyCellView> expected(cells.size()), views(cells.size());
    std::vector<std::uint8_t> endpoint;
    for (const auto threads : {1u, 12u}) {
        ma::oni::colony::stop(); ma::oni::colony::restore(cfg, mats, source, 1);
        auto options = saved; options.threads = threads; options.minimum_blocks_per_thread = 1; set_mac_kernel_default_options(options);
        ma::oni::colony::advance(1, .01, 2); ma::oni::colony::wait();
        MAOniColonyStatus st{}; ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        require(st.state == 1 && st.completed_frames == 1 && st.maximum_relative_mass_error < 1e-12 &&
            st.maximum_relative_energy_error < 1e-9, "projection control fault or open ledger");
        if (threads == 1) { expected = views; endpoint = ma::oni::colony::checkpoint(); }
        else require(std::memcmp(expected.data(), views.data(), views.size() * sizeof(MAOniColonyCellView)) == 0 &&
            ma::oni::colony::checkpoint() == endpoint, "projected views or continuation depend on workers");
    }
    ma::oni::colony::stop(); set_mac_kernel_default_options(saved);
    std::cout << "PASS multi-block colony projection: view bytes, cold continuation, germs and ledgers\n";
}
// A sealed pocket its compressed liquid fills completely (over-full water
// under a speck of gas): the gas waits in the reservoir, booked.
void flooded_pocket_control() {
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1000), material("Water", 13, water, false, .018, 4000, 1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION;
    cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = 3; cfg.grid_height = cfg.height = 4; cfg.gravity_m_per_s2 = 9.81;
    cfg.maximum_timestep_s = .01;
    std::vector<MAOniColonyCell> cells(12, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    cells[1 + 1 * 3] = {water, MA_ONI_COLONY_FLUID, 2500, 300, 0, 0};
    cells[1 + 2 * 3] = {oxygen, MA_ONI_COLONY_FLUID, 1e-6f, 300, 0, 0};
    ma::oni::colony::start(cfg, mats, cells, 1);
    const auto st = ma::oni::colony::status();
    std::cout << "flooded pocket: reservoir " << st.reservoir_kg << " kg\n";
    require(st.state == 1 && st.reservoir_kg > 499, "a flooded pocket starts, its overflow and gas in the reservoir");
    const auto before=ma::oni::colony::checkpoint();
    ma::oni::colony::sample_history(1);
    const std::array<std::uint32_t,2> requested{4,7};
    MAOniColonyStatus source{};std::vector<MAOniColonySampleCell> rows;
    std::vector<MAOniColonySamplePart> parts;double booked=-1;
    ma::oni::colony::sample_frame(0,requested,source,rows,parts,&booked,nullptr);
    double fluid=0;for(const auto& part : parts)fluid+=part.mass_kg;
    require(source.completed_frames==0 && source.reservoir_kg==st.reservoir_kg &&
        std::abs(fluid+source.reservoir_kg-booked)<1e-9 &&
        std::abs(booked-(2500+static_cast<double>(1e-6f)))<1e-9,
        "arena booked inventory omitted its same-frame held reservoir");
    require(ma::oni::colony::checkpoint()==before,"reservoir inventory observation changed checkpoint");
    ma::oni::colony::stop();
}
// The game-floor profile's trace sink: a sealed cell holding only a trace
// below the threshold loses it at the next frame, booked as its own ledger
// term (the ledgers stay closed), and the total survives a save and restore.
void trace_sink_control() {
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1000), material("CarbonDioxide", 12, carbon, true, .044, 850)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION; cfg.flags = MA_ONI_COLONY_GAME_FLOOR;
    cfg.vacuum_index = vacuum; cfg.grid_width = cfg.width = 5; cfg.grid_height = cfg.height = 3; cfg.gravity_m_per_s2 = 9.81;
    cfg.maximum_timestep_s = .01;
    std::vector<MAOniColonyCell> cells(15, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    cells[1 + 1 * 5] = {oxygen, MA_ONI_COLONY_FLUID, 1, 300, 0, 0};
    constexpr float trace_kg=1e-12f;
    cells[3 + 1 * 5] = {carbon, MA_ONI_COLONY_FLUID, trace_kg, 300, 0, 0};
    ma::oni::colony::start(cfg, mats, cells, 1);
    ma::oni::colony::advance(1, .2, 2); ma::oni::colony::wait();
    const auto st = ma::oni::colony::status();
    std::cout << "trace sink: state " << st.state << " " << st.error << ", sunk " << st.trace_sink_kg << " kg " << st.trace_sink_j
              << " J, mass error " << st.maximum_relative_mass_error << ", energy error " << st.maximum_relative_energy_error << "\n";
    require(st.state == 1 && st.trace_sink_kg == static_cast<double>(trace_kg) && st.maximum_relative_mass_error < 1e-12 &&
        st.maximum_relative_energy_error < 1e-12, "the profile sinks a sealed trace and books it");
    const auto image = ma::oni::colony::checkpoint();
    ma::oni::colony::stop();
    ma::oni::colony::restore(cfg, mats, image, 1);
    ma::oni::colony::advance(2, .2, 2); ma::oni::colony::wait();
    const auto restored = ma::oni::colony::status();
    require(restored.state == 1 && restored.trace_sink_kg == st.trace_sink_kg && restored.maximum_relative_mass_error < 1e-12,
        "a restored owner keeps its trace-sink ledger and stays closed");
    ma::oni::colony::stop();
}
}

// A machine's output with no open cell within two (callback -1) waits in the
// reservoir instead of being lost; a callback-bearing emission is refused.
void unplaced_output_control() {
    using namespace ma::oni::colony;
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1000)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION; cfg.vacuum_index = vacuum;
    cfg.grid_width = cfg.width = 9; cfg.grid_height = cfg.height = 3; cfg.maximum_timestep_s = .1;
    std::vector<MAOniColonyCell> cells(27, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    cells[1 + 9] = {oxygen, MA_ONI_COLONY_FLUID, 1.2f, 300, 0, 0};
    start(cfg, mats, cells, 1);
    const std::array<MAOniColonyTransfer, 2> outputs{{{7 + 9, oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, .25, 310, 0}, {7 + 9, oxygen, MA_ONI_COLONY_INJECT, 0, 42, 0, .5, 310, 0}}};
    queue_transfers(outputs); advance(1, .2, 2); wait();
    const auto st = status();
    MAOniColonyReceipt receipts[4]{}; const auto n = copy_receipts(receipts, 4);
    require(st.state == 1 && st.reservoir_kg == .25 && n == 1 && receipts[0].mass_kg == 0 && receipts[0].callback == 42,
        "an unplaced output was lost or a refused emission reported mass");
    ack_receipts(receipts[n - 1].sequence);
    const auto image = checkpoint(); stop(); restore(cfg, mats, image, 1);
    require(status().reservoir_kg == .25, "unplaced output did not survive a save");
    stop(); std::cout << "UNPLACED OUTPUT PASS\n";
}
// World germ evolution (docs/colony-owner.md#germs), made-up disease rules.
void world_biology_control() {
    using namespace ma::oni::colony;
    namespace bio = ma::oni::colony::biology;
    constexpr float inf = std::numeric_limits<float>::infinity();
    constexpr std::uint32_t elements = 10;
    // Disease 0 grows in oxygen (negative half-life) inside its density window
    // and dies at 0.5 germs/s below it; disease 1 decays in oxygen; neither
    // changes in carbon dioxide; water halves disease 0 every 20 s.
    std::array<MAOniColonyDisease, 2> diseases{};
    for (auto& d : diseases) {
        d.struct_size = sizeof(d); d.abi_version = 1; d.strength = 1;
        const float range[4]{250, 270, 330, 350}, lives[4]{10, 20, inf, 15};
        std::copy(range, range + 4, d.temperature_range); std::copy(lives, lives + 4, d.temperature_half_lives);
    }
    diseases[1].strength = 2;
    std::vector<MAOniColonyDiseaseGrowth> growth(2 * elements, MAOniColonyDiseaseGrowth{0, inf, inf, 0, 0, inf, 0, 0});
    growth[oxygen] = {.5f, -100, 50, .5f, 1, 1e6f, 10, 2};
    growth[water] = {0, 20, 20, .25f, 0, inf, 1, 0};
    growth[elements + oxygen] = {0, 30, 30, .5f, 0, inf, 1, 0};
    set_diseases(diseases, growth, elements);
    bio::Table table; table.elements = elements;
    for (const auto& d : diseases) { bio::Disease r; r.strength = d.strength; std::copy(d.temperature_range, d.temperature_range + 4, r.range.begin());
        std::copy(d.temperature_half_lives, d.temperature_half_lives + 4, r.half_lives.begin()); table.diseases.push_back(r); }
    for (const auto& g : growth) table.growth.push_back({g.under_population_death_rate, g.population_half_life, g.over_population_half_life, g.diffusion_scale,
        g.minimum_count_per_kg, g.maximum_count_per_kg, g.minimum_diffusion_count, static_cast<std::uint8_t>(g.minimum_diffusion_ticks)});
    bio::validate(table);
    // Temperature rules: bound values outside, linear between, none in the growth window.
    const auto& d0 = table.diseases[0];
    require(bio::temperature_half_life(d0, 240) == 10 && bio::temperature_half_life(d0, 260) == 15 && std::isinf(bio::temperature_half_life(d0, 300)) &&
        std::isinf(bio::temperature_half_life(d0, 340)) && bio::temperature_half_life(d0, 360) == 15, "temperature half-life rules");
    require(bio::frame_rate(inf) == 1 && bio::frame_rate(0) == 0 && bio::frame_rate(.2f) == .5f && bio::frame_rate(-.2f) == 2, "frame rate");
    bio::Medium air; air.open = true; air.state = 1; air.element = oxygen; air.temperature_k = 300; air.mass_kg = 1; air.parts = {{oxygen, 1}};
    // An inert disease can be present in the registry even though its
    // temperature rule has no effect. Do not disable every other disease
    // because its exact no-effect sentinel contains infinite bounds.
    {
        auto inert = table;
        inert.diseases[0].range.fill(inf); inert.diseases[0].half_lives.fill(inf);
        inert.growth.assign(inert.growth.size(), bio::Growth{0,inf,inf,0,0,inf,0,0});
        bio::validate(inert);
        for (const float temperature : {1.f,300.f,1e6f}) {
            auto medium = air; medium.temperature_k = temperature;
            require(bio::temperature_half_life(inert.diseases[0],temperature)==inf,
                "Idempotent temperature sentinel changed a finite temperature");
            for (const double held : {1000.,1000.5}) {
                const auto unchanged = bio::grow(inert,0,medium,held,{});
                require(unchanged.population==held && unchanged.change==0,
                    "Idempotent disease changed whole or expected populations");
            }
        }
        for (const bool half_life : {false,true}) {
            auto malformed = inert;
            (half_life?malformed.diseases[0].half_lives:malformed.diseases[0].range)[2]=300;
            bool refused=false;try {bio::validate(malformed);}catch(const std::invalid_argument&){refused=true;}
            require(refused,"A partial infinite temperature sentinel was accepted");
        }
        auto malformed = inert; malformed.diseases[0].range[0]=std::numeric_limits<float>::quiet_NaN();
        bool refused=false;try {bio::validate(malformed);}catch(const std::invalid_argument&){refused=true;}
        require(refused,"A NaN temperature sentinel was accepted");
    }
    // Whole-germ growth keeps the integer count and a binary32 residue; over
    // 500 frames (100 s) it follows the exponential to within a germ.
    double count = 1000; bio::Clock clock{};
    for (int f = 0; f < 500; ++f) {
        const auto step = bio::grow(table, 0, air, count, clock);
        require(step.clock.mode == bio::native_mode && step.population == std::floor(step.population) && std::abs(step.clock.residue) < 1 &&
            step.population == count + step.change, "whole-germ step");
        count = step.population; clock = step.clock;
    }
    const double exact = 1000 * std::exp2(1.0);
    std::cout << "whole-germ growth: 1000 -> " << count << " (continuous " << exact << ")\n";
    require(std::abs(count - exact) <= 1.5, "whole-germ growth departs from its half-life");
    // Below the density window (1 per kg of 10 kg): absolute death 0.1/frame, extinct after 30 frames.
    bio::Medium big = air; big.mass_kg = 10; big.parts = {{oxygen, 10}};
    count = 3; clock = {}; int frames = 0;
    while (count > 0 && frames < 100) { const auto step = bio::grow(table, 0, big, count, clock); count = step.population; clock = step.clock; ++frames; }
    require(count == 0 && frames >= 29 && frames <= 31, "under-population death");
    // Expected (non-integer) cultures follow the same rule in binary64; a
    // half-oxygen, half-carbon-dioxide medium changes at half the rate.
    bio::Medium mixed = air; mixed.parts = {{oxygen, .5}, {carbon, .5}};
    const auto full = bio::grow(table, 0, air, 1000.5, {}), half = bio::grow(table, 0, mixed, 1000.5, {});
    require(full.clock.mode == bio::expected_mode && full.change > 0 && std::abs(half.change - .5 * full.change) <= 1e-12 * full.change, "expected and blended growth");
    // Diffusion: an eighth of the difference, truncated, times the source scale.
    require(bio::diffused(100, 0, 1) == -12 && bio::diffused(0, 100, .5f) == 6 && bio::diffused(0, 10.75, 1) == 1, "diffusion amount");
    require(bio::diffusion_scale(table, 0, oxygen, 9, 5) == 0 && bio::diffusion_scale(table, 0, oxygen, 10, 1) == 0 &&
        bio::diffusion_scale(table, 0, oxygen, 10, 2) == .5f, "diffusion thresholds");
    // A 3 x 1 strip: the culture waits two frames, then spreads; germs are
    // conserved apart from booked births and deaths.
    {
        bio::Cells cultures{{0, {{1, 1000}}}}; bio::Clocks clocks; bio::Accounts accounts; std::vector<bio::Edit> edits;
        std::array<bio::Medium, 3> strip{air, air, air};
        const std::function<const bio::Medium&(std::size_t)> medium = [&](std::size_t i) -> const bio::Medium& { return strip[i]; };
        for (int f = 0; f < 3; ++f) bio::frame(table, 3, 1, medium, cultures, clocks, accounts, edits);
        require(cultures.count(1) == 1 && cultures.count(2) == 0, "diffusion waits for its minimum ticks");
        long double total = 0; for (const auto& [cell, list] : cultures) for (const auto& c : list) total += c.quantity;
        const auto& a = accounts[1];
        require(std::abs(total - (1000 + a.born - a.died - a.culled)) < 1e-9 && a.moved > 0, "strip germ account");
        // Sub-germ cultures die out.
        cultures = {{0, {{2, .75}}}}; clocks.clear(); accounts.clear(); edits.clear();
        bio::frame(table, 3, 1, medium, cultures, clocks, accounts, edits);
        require(cultures.empty() && accounts[2].culled > .7 && !edits.empty(), "sub-germ culture retained");
    }
    // Owner: a sealed 4 x 3 room of oxygen with both diseases in one cell.
    std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, true, .032, 1000), material("CarbonDioxide", 12, carbon, true, .044, 850)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION; cfg.vacuum_index = vacuum;
    cfg.grid_width = cfg.width = 6; cfg.grid_height = cfg.height = 5; cfg.maximum_timestep_s = .1;
    std::vector<MAOniColonyCell> cells(30, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    for (std::uint32_t y = 1; y <= 3; ++y) for (std::uint32_t x = 1; x <= 4; ++x) cells[x + 6 * y] = {oxygen, MA_ONI_COLONY_FLUID, 1.2f, 300, 0, 0};
    cells[1 + 6] = {oxygen, MA_ONI_COLONY_FLUID, 1.2f, 300, 1, 5000};
    start(cfg, mats, cells, 1);
    const MAOniColonyGermEdit second{7, 2, 4000};
    queue_germs(std::span(&second, 1));
    MAOniColonyBiology summary{}; std::vector<MAOniColonyBiologyAccount> accounts;
    std::uint64_t ticket = 0;
    const auto run = [&](int n) { for (int k = 0; k < n; ++k) { advance(++ticket, .2, 2); wait(); } require(status().state == 1, "biology owner faulted"); };
    run(20);
    germ_accounts(summary, accounts);
    require(summary.mode == 1 && summary.frames == 20 && accounts.size() == 2 && summary.audit_error < 1e-12, "owner germ accounts");
    const auto& grown = accounts[0]; const auto& decayed = accounts[1];
    std::cout << "owner germs: disease 1 held " << grown.held << " (born " << grown.born << ", moved " << grown.moved << "), disease 2 held "
              << decayed.held << " (died " << decayed.died << "), cells " << summary.germ_cells << "\n";
    require(grown.identity == 1 && grown.born > 0 && grown.moved > 0 && decayed.identity == 2 && decayed.died > 0 && decayed.edited_in == 4000 &&
        summary.germ_cells > 1, "both identities evolve in one cell and spread");
    // Version 10 save with clocks and a queued edit; cold continuation is byte exact.
    const MAOniColonyGermEdit late{8, 1, 25};
    queue_germs(std::span(&late, 1));
    const auto image = checkpoint();
    std::uint32_t version = 0; std::memcpy(&version, image.data() + 8, 4);
    require(version == 10, "world germ owner did not save version 10");
    run(15); const auto continued = checkpoint(); const auto first_session = status().session;
    stop(); restore(cfg, mats, image, 1);
    require(status().session != first_session, "restored owner reused its session");
    ticket = 0; run(15);
    require(checkpoint() == continued, "restored world germs did not continue byte for byte");
    stop();
    // Without a disease table, new colonies keep the legacy mode and version 8.
    set_diseases({}, {}, 0);
    start(cfg, mats, cells, 1); run(2);
    const auto legacy = checkpoint(); std::memcpy(&version, legacy.data() + 8, 4);
    germ_accounts(summary, accounts);
    require(version == 8 && summary.mode == 0, "legacy owner changed its saved version");
    stop();
    bool refused = false;
    try { set_diseases(diseases, std::span(growth.data(), growth.size() - 1), elements); } catch (const std::invalid_argument&) { refused = true; }
    require(refused, "inconsistent disease table admitted");
    std::cout << "WORLD BIOLOGY PASS\n";
}
void retirement_control() {
    using namespace ma::oni::colony;using ma::experimental::MacEnergySum;
    namespace detail=ma::oni::colony::detail;
    struct Phase {std::map<std::uint32_t,MacEnergySum> mass,germs;MacEnergySum energy,remainder;};
    const auto read=[&](std::uint64_t token) {
        std::vector<std::uint8_t> wire(copy_retirement(token,nullptr,0));
        require(copy_retirement(token,wire.data(),static_cast<std::uint32_t>(wire.size()))==wire.size(),"Retirement copy size changed");
        detail::ParcelWireReader r{wire};const auto magic=r.raw(8);
        require(std::string_view(reinterpret_cast<const char*>(magic.data()),8)=="MARET001","Wrong retirement wire");
        require(r.u64()==status().session && r.u64()==status().completed_frames,"Retirement source labels changed");
        (void)r.u32();(void)r.u32();(void)r.u32();
        const auto amount=[&] {MacEnergySum value;const auto n=r.u32();for(std::uint32_t i=0;i<n;++i)value.add(r.f64());return value;};
        std::array<Phase,2> result;
        for(auto& p:result) {
            const auto n=r.u32();for(std::uint32_t i=0;i<n;++i){const auto id=r.u32();p.mass.emplace(id,amount());}
            p.energy=amount();const auto germs=r.u32();for(std::uint32_t i=0;i<germs;++i){const auto id=r.u32();p.germs.emplace(id,amount());}
            for(int i=0;i<4;++i)(void)r.sum();
            p.remainder=amount();
        }
        require(r.at==wire.size(),"Retirement inventory left trailing bytes");return result;
    };
    const auto equal=[](MacEnergySum a,const MacEnergySum& b) {detail::add_precision_sum(a,b,-1);return a.sign()==0;};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850),
        material("Water",13,water,false,.018,4180,1000)};
    std::vector<MAOniColonyCell> cells(9);cells[0]={oxygen,MA_ONI_COLONY_FLUID,1,300,1,1000};cells[4]={oxygen,MA_ONI_COLONY_FLUID,2,300,0,0};
    start(cfg,mats,cells,1);enable_precise_receipts(true);
    const std::array<MAOniColonyTransfer,2> additions{{{0,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.25,300,0},{0,water,MA_ONI_COLONY_INJECT,0,-1,0,2,300,0}}};
    queue_transfers(additions);require(flush_inputs()==0,"Retirement source did not settle");
    // Include an actual sub-ULP owner-held parcel, not just visible grid mass.
    const MAOniColonyTransfer tiny{0,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,1e-240,300,0};
    (void)queue_pipe_job(1,tiny,{});require(flush_inputs()==0,"Retirement precision source did not settle");
    MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1,"Retirement precision source lost its receipt");ack_receipts(receipt.sequence);
    require(!detail::decode_precision_inventory(precision_inventory()).empty(),"Retirement source has no actual precision inventory");
    const auto initial=checkpoint();bool refused=false;
    try{(void)prepare_retirement(2,2,1);}catch(const std::invalid_argument&){refused=true;}
    require(refused && checkpoint()==initial,"Invalid retirement changed the source");
    auto prepared=prepare_retirement(0,3,1);cancel_retirement(prepared.first);
    require(checkpoint()==initial,"Preparing/cancelling retirement changed the source");
    prepared=prepare_retirement(0,3,1);const MAOniColonyHeat heat{0,0,1,0};queue_heat(std::span(&heat,1));
    refused=false;try{commit_retirement(prepared.first);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"A stale retirement ignored newly queued work");cancel_retirement(prepared.first);require(flush_inputs()==0,"Late retirement heat did not settle");
    prepared=prepare_retirement(0,3,1);const auto inventory=read(prepared.first);MacEnergySum expected;expected.add(1);expected.add(1e-240);
    require(equal(inventory[0].mass.at(oxygen),expected),"Retirement omitted or rounded a precision parcel");
    expected={};expected.add(.25);require(equal(inventory[0].mass.at(carbon),expected),"Retirement omitted minority gas");
    expected={};expected.add(2);require(equal(inventory[1].mass.at(water),expected),"Retirement omitted hidden liquid");
    MacEnergySum germs=inventory[0].germs.at(0);detail::add_precision_sum(germs,inventory[1].germs.at(0));
    expected={};expected.add(1000);require(equal(germs,expected),"Retirement lost fractional germ shares between phases");
    const auto before=status();commit_retirement(prepared.first);require(status().completed_frames==before.completed_frames && status().time_seconds==before.time_seconds,
        "Retirement consumed simulation time");
    std::array<std::uint32_t,4> ids{};std::array<double,4> masses{};
    require(composition(0,ids.data(),masses.data(),4)==0 && composition(4,ids.data(),masses.data(),4)==1 && masses[0]==2,
        "Retirement kept duplicate source mass or changed the other world");
    require(detail::decode_precision_inventory(precision_inventory()).empty(),"Retirement left an orphan precision anchor");
    refused=false;try{(void)checkpoint();}catch(const std::invalid_argument&){refused=true;}require(refused,"Saving admitted an unfinished native grid teardown");
    refused=false;try{finish_retirement(0,3,1);}catch(const std::invalid_argument&){refused=true;}require(refused,"Retirement released still-open cells");
    const std::array<MAOniColonyTerrain,3> closing{{{0,MA_ONI_COLONY_CLOSE},{1,MA_ONI_COLONY_CLOSE},{2,MA_ONI_COLONY_CLOSE}}};
    queue_terrain(closing);require(flush_inputs()==0,"Empty retirement cells did not close");finish_retirement(0,3,1);
    advance(1,.2,2);wait();require(status().state==1 && status().completed_frames==1,
        "A warm physical tick failed after retirement and before any cold restore");
    const auto saved=checkpoint();stop();restore(cfg,mats,saved,1);require(checkpoint()==saved,"Finished retirement did not cold-reencode exactly");
    const MAOniColonyTerrain opening{0,MA_ONI_COLONY_OPEN};queue_terrain(std::span(&opening,1));
    auto reused=tiny;reused.mass_kg=.5;queue_transfers(std::span(&reused,1));require(flush_inputs()==0,"Reused rocket cell did not open");
    require(composition(0,ids.data(),masses.data(),4)==1 && ids[0]==oxygen && masses[0]==.5,"A reused rocket cell inherited a former mixture");stop();
    // A closed world's retained reservoir is already outside the CFD ledger;
    // recover it once without debiting that ledger again.
    cfg.grid_width=cfg.width=5;cells.assign(15,{});cells[14]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    start(cfg,mats,cells,1);auto reservoir=tiny;reservoir.native_index=water;reservoir.mass_kg=1500;
    queue_transfers(std::span(&reservoir,1));require(flush_inputs()==0 && status().reservoir_kg==1500,"Closed-world reservoir source was not retained");
    prepared=prepare_retirement(0,1,1);const auto stored=read(prepared.first);expected={};expected.add(1500);
    require(equal(stored[1].mass.at(water),expected),"Retirement omitted its retained reservoir");
    commit_retirement(prepared.first);finish_retirement(0,1,1);require(status().reservoir_kg==0 && status().state==1,"Reservoir retirement double-debited or retained its source");
    require(status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-12,"Retirement ledgers do not close");stop();
    std::cout<<"PASS world retirement: atomic preparation/stale refusal, full mixed phases/germs/precision/reservoir, quarantine/save guard, cold restore and clean slot reuse\n";
}
void input_flush_control() {
    using namespace ma::oni::colony;
    const auto flush_from_native_caller=[] {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        // A Unity/native caller may leave x87 registers occupied. Saving the
        // rounding/control words alone does not free those registers. The
        // owner must execute on its own worker and preserve the caller's FP
        // state; force the occupied-stack case instead of relying on luck.
        alignas(16) std::array<unsigned char,512> saved{};
        __asm__ volatile("fxsave %0" : "=m"(saved));
        __asm__ volatile("fninit; fldz; fldz; fldz; fldz; fldz; fldz; fldz; fldz");
        try {
            const auto result=flush_inputs();
            __asm__ volatile("fxrstor %0" : : "m"(saved));return result;
        } catch(...) {__asm__ volatile("fxrstor %0" : : "m"(saved));throw;}
#else
        return flush_inputs();
#endif
    };
    // Adjacent empty cells expose accidental flow; a separate space source
    // exposes an accidental sink. A live growth rule exposes biology ticks.
    const float inf=std::numeric_limits<float>::infinity();
    MAOniColonyDisease disease{};disease.struct_size=sizeof(disease);disease.abi_version=1;disease.strength=1;
    std::fill(std::begin(disease.temperature_range),std::end(disease.temperature_range),inf);
    std::fill(std::begin(disease.temperature_half_lives),std::end(disease.temperature_half_lives),inf);
    std::vector<MAOniColonyDiseaseGrowth> growth(10,{0,inf,inf,0,0,inf,0,0});
    growth[oxygen]={0,-1,inf,1,0,inf,1,0};
    set_diseases(std::span(&disease,1),growth,10);
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850)};
    std::vector<MAOniColonyCell> cells(9);cells[4]={oxygen,MA_ONI_COLONY_FLUID,1,300,1,100};
    cells[8]={vacuum,MA_ONI_COLONY_SPACE,0,0,0,0};
    start(cfg,mats,cells,1);enable_precise_receipts(true);sample_history(4);
    const auto initial=checkpoint();
    require(flush_inputs()==0 && checkpoint()==initial,"An empty input flush changed the checkpoint");
    const auto mass=[](std::uint32_t cell) {
        std::array<std::uint32_t,4> ids{};std::array<double,4> amounts{};double total=0;
        const auto count=composition(cell,ids.data(),amounts.data(),4);
        for(std::uint32_t k=0;k<count;++k)total+=amounts[k];
        return total;
    };
    const std::array<MAOniColonyTransfer,2> sources{{{4,carbon,MA_ONI_COLONY_INJECT,0,77,0,.25,300,0},
        {8,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.5,300,0}}};
    const MAOniColonyHeat heat{4,0,5,0};const MAOniColonyGermEdit germ{4,1,3};
    const std::array<MAOniColonyTerrain,2> openings{{{0,MA_ONI_COLONY_OPEN},{5,MA_ONI_COLONY_OPEN}}};
    queue_transfers(sources);queue_heat(std::span(&heat,1));queue_germs(std::span(&germ,1));queue_terrain(openings);
    require(flush_from_native_caller()==0,"Valid lifecycle inputs remained pending");
    const auto settled=status();
    std::cout<<"lifecycle inputs: masses "<<mass(4)<<' '<<mass(5)<<' '<<mass(8)<<", sinks "<<settled.space_sink_kg<<' '<<settled.trace_sink_kg<<", heat "<<settled.committed_heat<<'\n';
    require(settled.state==1 && settled.completed_frames==0 && settled.native_ticket==0 && settled.time_seconds==0 && settled.substeps==0,
        "Input flushing advanced the owner's clock");
    require(mass(4)==1.25 && mass(5)==0 && mass(8)==.5 && settled.space_sink_kg==0 && settled.trace_sink_kg==0 && settled.committed_heat==1,
        "Input flushing omitted an edit or ran a physical stage");
    MAOniColonyBiology biology{};std::vector<MAOniColonyBiologyAccount> accounts;germ_accounts(biology,accounts);
    require(biology.frames==0 && accounts.size()==1 && accounts[0].held==103 && accounts[0].born==0 && accounts[0].moved==0,
        "Input flushing grew, diffused or lost queued germs");
    MAOniColonyStatus copied{};std::array<MAOniColonyCellView,9> views{};
    require(needs_shadow_refresh() && copy_changed(settled.session,0,copied,views.data(),9) && views[8].mass_kg==.5f && (views[0].flags&MA_ONI_COLONY_VIEW_OWNED),
        "Frame-only view caching hid a lifecycle edit");
    std::vector<MAOniColonySampleCell> sample_cells;std::vector<MAOniColonySamplePart> sample_parts;
    const std::array<std::uint32_t,1> selection{8};
    sample_frame(0,selection,copied,sample_cells,sample_parts,nullptr,nullptr);
    require(sample_parts.size()==1 && sample_parts[0].mass_kg==.5,"Same-frame sampling retained the pre-edit snapshot");
    MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1 && receipt.callback==77 && receipt.completed_frame==0 && receipt.mass_kg==.25f,
        "A lifecycle callback did not retain its current-frame label");ack_receipts(receipt.sequence);
    // Refused work must stay visible to the caller, rather than being called
    // quiescent. Nothing can flow into a vacuum neighbour in this operation.
    refuse_heat(4);queue_heat(std::span(&heat,1));
    require(flush_inputs()==1 && status().committed_heat==1,"Deferred lifecycle heat was silently discarded");
    refuse_heat(-1);require(flush_inputs()==0 && status().committed_heat==2,"Deferred lifecycle heat did not retry");
    const MAOniColonyTransfer take{4,0,MA_ONI_COLONY_EXTRACT_GAS,0,-1,0,2,0,0};
    (void)queue_pipe_job(1,take,{});require(flush_inputs()==0,"Durable lifecycle extraction did not settle");
    std::array<MAOniColonyReceipt,4> receipts{};const auto count=copy_receipts(receipts.data(),4);double taken=0;
    require(count==2 && pipe_job_state(1).first==2,"A mixed lifecycle extraction lost its durable outcomes");
    for(std::uint32_t k=0;k<count;++k) {
        require(receipts[k].completed_frame==0 && receipt_job(receipts[k].sequence)==1 && copy_precise_receipt(receipts[k].sequence,nullptr,0)>0,
            "A lifecycle constituent lost its precise receipt");taken+=receipts[k].mass_kg;
    }
    require(taken==1.25 && mass(4)==0,"Lifecycle evacuation lost a constituent or left duplicate mass");
    ack_receipts(receipts[count-1].sequence);
    const auto saved=checkpoint();stop();restore(cfg,mats,saved,1);
    require(checkpoint()==saved && status().time_seconds==0,"A zero-time lifecycle edit failed exact cold reencoding");
    advance(1,.2,2);wait();const auto advanced=status();
    require(advanced.state==1 && advanced.completed_frames==1 && advanced.native_ticket==1 && advanced.time_seconds>0 && advanced.space_sink_kg>0,
        "A positive advance stopped working after lifecycle edits");
    require(!needs_shadow_refresh() && !copy_changed(advanced.session,advanced.completed_frames,copied,views.data(),9),"A positive advance did not restore ordinary frame caching");
    require(advanced.maximum_relative_mass_error<1e-12 && advanced.maximum_relative_energy_error<1e-12,"Lifecycle edit ledgers do not close");
    // Repeat after a real tick has populated cached kernel audit sums. New
    // topology and a sub-threshold source must be audited as actual cells,
    // without a flow tick or trace sink refreshing the snapshot for us.
    const MAOniColonyTerrain late_open{2,MA_ONI_COLONY_OPEN};queue_terrain(std::span(&late_open,1));
    const std::array<MAOniColonyTransfer,2> late_sources{{{2,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.125,300,0},
        {0,carbon,MA_ONI_COLONY_INJECT,0,-1,0,1e-13,300,0}}};
    const auto prior_zero=mass(0);queue_transfers(late_sources);
    require(flush_inputs()==0 && status().state==1 && status().completed_frames==advanced.completed_frames && status().time_seconds==advanced.time_seconds &&
        mass(2)==.125 && mass(0)==prior_zero+1e-13 && status().trace_sink_kg==advanced.trace_sink_kg,
        "A warmed lifecycle boundary used stale audit totals, lost a trace or advanced time");
    sample_history(0);stop();set_diseases({}, {},0);
    bool refused=false;try{(void)flush_inputs();}catch(const std::invalid_argument&){refused=true;}
    require(refused,"Input flushing admitted an absent owner");
    std::cout<<"PASS lifecycle input flush: all constituents, germs, deferred heat, durable receipts, same-frame cache, no time/flow/sink, cold reencoding\n";
}
void cluster_boundary_control() {
    using namespace ma::oni::colony;
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE;cfg.vacuum_index=vacuum;
    cfg.grid_width=cfg.width=3;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005)};
    const std::vector<MAOniColonyCell> cells(9);
    start(cfg,mats,cells,1);
    const std::array<std::uint32_t,1> first{0},last{8};
    set_space_cells(first,true);
    bool refused=false;
    try {const std::array<std::uint32_t,2> invalid{8,9};set_space_cells(invalid,true);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"Cluster boundary admitted an out-of-grid cell");
    const std::array<MAOniColonyTerrain,2> openings{{{0,MA_ONI_COLONY_OPEN},{8,MA_ONI_COLONY_OPEN}}};
    queue_terrain(openings);
    const std::array<MAOniColonyTransfer,2> sources{{{0,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,1,300,0},
        {8,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,2,300,0}}};
    queue_transfers(sources);
    const auto initial=checkpoint();stop();restore(cfg,mats,initial,1);
    require(checkpoint()==initial,"Cluster boundary flags or queued opening changed on restore");
    const auto tick=[] {advance(1,.2,2);wait();require(status().state==1,status().error);};
    const auto mass=[](std::uint32_t cell) {
        std::array<std::uint32_t,2> ids{};std::array<double,2> held{};
        const auto count=composition(cell,ids.data(),held.data(),2);double total=0;
        for(std::uint32_t k=0;k<count;++k)total+=held[k];
        return total;
    };
    tick();
    require(mass(0)==0 && mass(8)==2 && status().space_sink_kg==1,
        "A new space cell did not sink, or an invalid batch changed its valid prefix");
    const auto frame=status().completed_frames;
    set_space_cells(first,false);set_space_cells(last,true);
    require(status().completed_frames==frame && mass(8)==2,"Changing cluster boundaries advanced time or deleted inventory immediately");
    const auto reused=checkpoint();stop();restore(cfg,mats,reused,1);
    require(checkpoint()==reused,"Reused rocket space/interior flags failed exact restore");
    queue_transfers(std::span(sources.data(),1));tick();
    require(mass(0)==1 && mass(8)==0 && status().space_sink_kg==3,
        "A reused interior retained its old space sink, or a restored space cell stopped sinking");
    require(status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-12,
        "Dynamic cluster boundary mass or energy ledger did not close");
    stop();std::cout<<"PASS cluster boundaries: atomic validation, new vacuum/space, reused cells, no zero-time deletion, exact cold reencoding\n";
}
// A successful production-kernel heat edit replaces the state snapshot.
// Refused edits must still examine neighbours after that replacement.
void heat_neighbour_bounds_control() {
    using namespace ma::oni::colony;
    const std::array mats{material("MadeUpGas", 111, oxygen, true, .032, 1000)};
    for (const auto flags : {0u, MA_ONI_COLONY_GAME_KERNEL}) {
        MAOniColonyConfig cfg{};
        cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION;
        cfg.flags = flags; cfg.vacuum_index = vacuum;
        cfg.grid_width = cfg.width = 3; cfg.grid_height = cfg.height = 3;
        cfg.maximum_timestep_s = .1;
        std::array<MAOniColonyCell, 9> cells;
        cells.fill({oxygen, MA_ONI_COLONY_FLUID, 1, 300, 0, 0});
        cells[0] = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
        start(cfg, mats, cells, 1);
        struct Cleanup { ~Cleanup() { refuse_heat(-1); stop(); } } cleanup;
        std::vector<MAOniColonyReading> before, after;
        std::vector<MAOniColonyConstituent> parts;
        (void)readings(before, parts);
        const MAOniColonyTerrain opening{0, MA_ONI_COLONY_OPEN};
        queue_terrain(std::span(&opening, 1));
        const std::array<MAOniColonyHeat, 2> heat{{{4, 0, 10, 1000}, {7, 0, 20, 1000}}};
        queue_heat(heat);
        const auto pending = checkpoint();
        const auto finish = [&] {
            refuse_heat(7);
            require(flush_inputs() == 0, "Accepted/refused heat did not settle after topology change");
            refuse_heat(-1);
            const auto settled = status();
            require(settled.state == 1 && settled.completed_frames == 0 && settled.time_seconds == 0 &&
                    settled.opened_cells == 1 && settled.committed_heat == 2 &&
                    settled.maximum_relative_mass_error < 1e-12 && settled.maximum_relative_energy_error < 1e-12,
                    "Heat neighbour fallback changed time, topology, commits or ledgers");
            (void)readings(after, parts);
            const auto energy = [](const auto& rows, std::uint32_t cell) {
                const auto row = std::find_if(rows.begin(), rows.end(), [=](const auto& r) { return r.public_cell == cell; });
                require(row != rows.end(), "Heat neighbour control lost a fluid reading");
                return row->internal_energy_j;
            };
            require(std::abs(energy(after, 4) - energy(before, 4) - 10) < 1e-8 &&
                    std::abs(energy(after, 6) - energy(before, 6) - 20) < 1e-8 &&
                    energy(after, 7) == energy(before, 7),
                    "Accepted then refused heat changed neighbour order or lost energy");
            return checkpoint();
        };
        const auto expected = finish();
        stop(); restore(cfg, mats, pending, 1);
        require(finish() == expected, "Heat neighbour fallback changed cold continuation bytes");
    }
    std::cout << "PASS heat neighbour bounds: topology change, accepted/refused order, both kernels, conserved energy, exact cold continuation\n";
}

#include "oni_activity_owner_control.hpp"
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "--heat-neighbour-bounds-only") == 0) { heat_neighbour_bounds_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--activity-only") == 0) {activity_owner_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--passives-only") == 0) {passive_observation_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--biology-only") == 0) {world_biology_control();unplaced_output_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--cluster-boundaries-only") == 0) {cluster_boundary_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--input-flush-only") == 0) {input_flush_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--retirement-only") == 0) {retirement_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--quiet-game-cell-only") == 0) {quiet_game_cell_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--shadow-handoff-only") == 0) {shadow_handoff_control();return 0;}
        if (argc == 2 && std::strcmp(argv[1], "--timestep-only") == 0) { timestep_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--projection-only") == 0) { projection_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--precise-receipts-only") == 0) { precise_receipt_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--floor-capture-only") == 0) { floor_capture_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--transfer-capture-only") == 0) { transfer_capture_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--release-capture-only") == 0) { release_capture_control();exact_release_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--pipe-jobs-only") == 0) { moving_job_control(); pipe_job_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--precision-inventory-only") == 0) { precision_inventory_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--airflow-boundary-only") == 0) { airflow_boundary_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--tiny-injection-only") == 0) { tiny_injection_neighbour_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--parcel-wire-only") == 0) { parcel_wire_control(); parcel_outcome_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--falling-solid-only") == 0) { falling_solid_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--sample-only") == 0) { sample_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--view-cache-only") == 0) { view_cache_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--actor-batch-only") == 0) { actor_batch_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--callback-rebind-only") == 0) { callback_rebind_control(); return 0; }
        if (argc == 2 && std::strcmp(argv[1], "--sample-history-only") == 0) { sample_history_control();native_sample_history_control();region_energy_control();return 0; }
        require(argc == 1, "Usage: ma_oni_colony_tests [--projection-only|--timestep-only|--precise-receipts-only]");
        heat_neighbour_bounds_control();
        activity_owner_control();
        precise_receipt_control();
        transfer_capture_control();
        floor_capture_control();
        falling_solid_control();
        release_capture_control();exact_release_control();
        parcel_wire_control();
        parcel_outcome_control();
        parcel_context_control();
        passive_observation_control();
        world_biology_control();
        cluster_boundary_control();
        input_flush_control();
        retirement_control();
        unplaced_output_control();
        quiet_game_cell_control();
        shadow_handoff_control();
        moving_job_control();
        tiny_injection_neighbour_control();
        airflow_boundary_control();
        precision_inventory_control();
        pipe_job_control();
        sample_control();
        view_cache_control();
        actor_batch_control();
        callback_rebind_control();
        sample_history_control();
        native_sample_history_control();
        region_energy_control();
        conversion_batch_control();
        timestep_control();
        projection_control();
        flooded_pocket_control();
        trace_sink_control();
        std::vector<MAOniColonyMaterial> materials{material("Oxygen", 11, oxygen, true, .016, 1000), material("CarbonDioxide", 12, carbon, true, .044, 850),
            material("Water", 13, water, false, .018, 4000, 1000), material("Steam", 14, steam, true, .018, 4000),
            material("SaltWater", 15, brine, false, .021, 4100, 1100)};
        // Salt water boils to steam and leaves 7% salt (hash 777, a solid).
        materials[4].high_temperature_k = 372.5; materials[4].high_target = 14; materials[4].high_byproduct = 777; materials[4].high_byproduct_fraction = .07;
        // Water boils to steam at 372.5 K and steam condenses back: a saturation pair.
        materials[2].high_temperature_k = 372.5; materials[2].high_target = 14; materials[2].low_target = 99;
        materials[3].low_temperature_k = 372.5; materials[3].low_target = 13;
        // A full cell of water at one atmosphere: the reference density, just
        // above the zero-pressure density; a gas has none.
        {
            const double atmosphere = ma::oni::colony::liquid_density(materials, water, 101325), zero = ma::oni::colony::liquid_density(materials, water, 0);
            std::cout << "water full cell: " << atmosphere << " kg at 1 atm, " << zero << " kg at 0 Pa\n";
            require(atmosphere > zero && atmosphere < zero * 1.001, "a full cell of water holds slightly more at one atmosphere");
            bool refused = false;
            try { (void)ma::oni::colony::liquid_density(materials, oxygen, 101325); } catch (const std::exception&) { refused = true; }
            require(refused, "a gas has no liquid density");
        }
        MAOniColonyConfig config{}; config.struct_size = sizeof(config); config.abi_version = MA_ONI_COLONY_ABI_VERSION;
        config.flags = MA_ONI_COLONY_PHYSICS_CONDUCTION; config.vacuum_index = vacuum; config.grid_width = GW; config.grid_height = GH;
        config.offset_x = X0; config.offset_y = Y0; config.width = W; config.height = H; config.gravity_m_per_s2 = 9.81; config.maximum_timestep_s = .01;
        std::vector<MAOniColonyCell> cells(W * H);
        for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) {
            auto& c = cells[x + y * W];
            const bool rock = x == 0 || x == W - 1 || y == 0 || y == 8 || (x == 11 && y == 5);
            if (rock) c = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
            else if (y >= 9) c = {vacuum, MA_ONI_COLONY_SPACE, 0, 0, 0, 0};
            else if (y <= 2 && x <= 7) c = {water, MA_ONI_COLONY_FLUID, 1000.f * (1.f + .01f * static_cast<float>(x % 3)), 290, 0, 0};
            else c = {x % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID, 1.2f + .1f * static_cast<float>(y), 300, 0, 0};
        }
        ma::oni::colony::start(config, materials, cells, 1);
        require(ma::oni::colony::active(), "colony active");
        // Suppression: owned fluid, solids, outside, and whole-rectangle transitions.
        using ma::oni::colony::suppress;
        require(suppress(MA_ONI_PRESSURE, native_cell(4, 4)) && !suppress(MA_ONI_PRESSURE, native_cell(0, 4)), "owned fluid cells only");
        require(!suppress(MA_ONI_PRESSURE, 0) && !suppress(MA_ONI_STATE_TRANSITION, native_cell(0, 4)) &&
            suppress(MA_ONI_STATE_TRANSITION, native_cell(4, 4)), "rectangle and transitions");
        require(ma::oni::colony::allow_native_transition(native_cell(3, 1), water, 200) &&
            !ma::oni::colony::allow_native_transition(native_cell(3, 1), water, 400) &&
            !ma::oni::colony::allow_native_transition(native_cell(4, 4), steam, 300), "only freezing runs natively on owned cells");
        // Transfers with receipts and a heat edit, committed at the next frame.
        const std::vector<MAOniColonyTransfer> transfers{{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, 7, 0, .5, 310, 0},
            {public_cell(9, 5), carbon, MA_ONI_COLONY_EXTRACT, 2, 8, 0, 3, 0, 0}, {public_cell(9, 5), 77, MA_ONI_COLONY_EXTRACT, 1, 9, 0, 1, 0, 0},
            {public_cell(6, 10), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, 1e-3, 300, 0}};
        ma::oni::colony::queue_transfers(transfers);
        const MAOniColonyHeat heat{public_cell(5, 1), 0, 2e5, 0};
        ma::oni::colony::queue_heat(std::span(&heat, 1));
        std::vector<std::uint8_t> image;
        std::vector<MAOniColonyCellView> views(W * H), continued(W * H);
        for (std::uint64_t frame = 1; frame <= 6; ++frame) {
            if (frame == 5) image = ma::oni::colony::checkpoint();
            ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait();
            const auto s = ma::oni::colony::status();
            if (s.state != 1) throw std::runtime_error(std::string("colony faulted: ") + s.error);
        }
        auto s = ma::oni::colony::status();
        MAOniColonyStatus st{}; ma::oni::colony::copy(st, continued.data(), static_cast<std::uint32_t>(continued.size()));
        std::cout << "frames " << s.completed_frames << " substeps " << s.substeps << " rejected " << s.rejected_attempts << " sink " << s.space_sink_kg
                  << " kg mass error " << s.maximum_relative_mass_error << " energy error " << s.maximum_relative_energy_error
                  << " reservoir " << s.reservoir_kg << " last " << s.last_advance_ms << " ms\n";
        std::cout << "evaporated " << s.evaporated_kg << " kg condensed " << s.condensed_kg << " kg skipped " << s.skipped_relaxations << '\n';
        require(s.completed_frames == 6 && s.committed_transfers == 4 && s.committed_heat == 1, "frames and commits");
        // The pool's surface evaporates into the air above it toward saturation.
        require(s.evaporated_kg > 0 && s.skipped_relaxations == 0, "phase change at the pool surface");
        require(s.space_sink_kg > 0, "gas reaching space leaves through the sink");
        require(s.maximum_relative_mass_error < 1e-12 && s.maximum_relative_energy_error < 1e-9, "ledgers close");
        std::vector<MAOniColonyReceipt> receipts(8); const auto count = ma::oni::colony::copy_receipts(receipts.data(), 8);
        require(count == 3 && receipts[0].callback == 7 && receipts[0].mass_kg == .5f && receipts[0].kind == MA_ONI_COLONY_INJECT, "injection receipt");
        require(receipts[1].callback == 8 && receipts[1].mass_kg == 3.f && receipts[1].temperature_k > 250, "radius extraction receipt");
        require(receipts[2].callback == 9 && receipts[2].mass_kg == 0, "unknown element extracts nothing");
        ma::oni::colony::ack_receipts(receipts[2].sequence);
        require(ma::oni::colony::copy_receipts(nullptr, 0) == 0, "acknowledged");
        // Projection and composition.
        const auto& pool = continued[3 + 1 * W];
        require(pool.element_index == water && (pool.flags & MA_ONI_COLONY_VIEW_LIQUID) && pool.mass_kg > 999 && pool.pressure_pa > 0, "pool view");
        require(continued[0].flags == 0 && continued[5 + 10 * W].element_index == vacuum, "solid and space views");
        std::uint32_t indices[8]; double masses[8];
        const auto parts = ma::oni::colony::composition(public_cell(10, 6), indices, masses, 8);
        require(parts >= 1 && parts <= 3, "gas composition");
        // Exact continuation from the frame-5 checkpoint.
        ma::oni::colony::stop(); require(!ma::oni::colony::active(), "stopped");
        ma::oni::colony::restore(config, materials, image, 1);
        ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait(); ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
        require(!ma::oni::colony::busy(), "solver idle after its frames");
        ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        require(st.state == 1 && st.completed_frames == 6, "restored frame count");
        require(std::memcmp(views.data(), continued.data(), views.size() * sizeof(MAOniColonyCellView)) == 0, "exact continuation");
        // Terrain: dig an interior wall beside gas, build over a gas cell.
        require(!suppress(MA_ONI_PRESSURE, native_cell(11, 5)) && suppress(MA_ONI_PRESSURE, native_cell(13, 6)), "terrain before");
        const std::vector<MAOniColonyTerrain> terrain{{public_cell(11, 5), MA_ONI_COLONY_OPEN}, {public_cell(13, 6), MA_ONI_COLONY_CLOSE}};
        ma::oni::colony::queue_terrain(terrain);
        ma::oni::colony::advance(7, .2, 2); ma::oni::colony::wait();
        ma::oni::colony::copy(st, views.data(), static_cast<std::uint32_t>(views.size()));
        if (st.state != 1) throw std::runtime_error(std::string("terrain faulted: ") + st.error);
        const auto& dug = views[11 + 5 * W]; const auto& built = views[13 + 6 * W]; const auto& beside = views[10 + 5 * W];
        std::cout << "dug " << dug.pressure_pa << " Pa beside " << beside.pressure_pa << " Pa, opened " << st.opened_cells << " closed " << st.closed_cells
                  << " mass error " << st.maximum_relative_mass_error << " energy error " << st.maximum_relative_energy_error << '\n';
        require(st.opened_cells == 1 && st.closed_cells == 1, "terrain counts");
        require((dug.flags & MA_ONI_COLONY_VIEW_GAS) && dug.pressure_pa > .5 * beside.pressure_pa, "dug cell refilled from its neighbours");
        require(built.flags == 0, "built cell is solid");
        require(suppress(MA_ONI_PRESSURE, native_cell(11, 5)) && !suppress(MA_ONI_PRESSURE, native_cell(13, 6)), "terrain ownership follows");
        require(st.maximum_relative_mass_error < 1e-12 && st.maximum_relative_energy_error < 1e-9, "terrain ledgers close");
        // Native shadow: a body warms one owned gas cell by 2 K, a native
        // process takes 0.1 kg from a mixed gas cell and empties another; all
        // become booked owner inputs, and the takes leave as gas mixtures.
        {
            const auto mix = [](std::uint32_t x, std::uint32_t y) {
                std::uint32_t idx[8]; double m[8]; std::array<double, 3> t{};
                const auto n = std::min<std::uint32_t>(ma::oni::colony::composition(public_cell(x, y), idx, m, 8), 8);
                for (std::uint32_t q = 0; q < n; ++q) if (idx[q] != water) t[idx[q] == oxygen ? 0 : idx[q] == carbon ? 1 : 2] += m[q];
                return t; // oxygen, carbon dioxide, steam
            };
            const auto gas_totals = [&] {
                std::array<double, 3> t{};
                for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) { const auto c = mix(x, y); for (int q = 0; q < 3; ++q) t[q] += c[q]; }
                return t;
            };
            // Seal (14, 7) off: native empties it, and no neighbour refills it.
            const MAOniColonyTerrain seal[] = {{public_cell(13, 7), MA_ONI_COLONY_CLOSE}, {public_cell(14, 6), MA_ONI_COLONY_CLOSE}};
            ma::oni::colony::queue_terrain(seal); ma::oni::colony::advance(8, .2, 2); ma::oni::colony::wait();
            const std::size_t N = (GW + 2) * (GH + 2);
            std::vector<std::uint16_t> el[2]; std::vector<float> mass[2], temperature[2];
            ma::oni::colony::NativeCells nc; nc.pitch = GW + 2; nc.rows = GH + 2;
            for (int b = 0; b < 2; ++b) {
                el[b].assign(N, 0); mass[b].assign(N, 0); temperature[b].assign(N, 300);
                for (std::size_t c = 0; c < N; ++c) if (ma::oni::colony::owns(static_cast<int>(c))) el[b][c] = vacuum;
                nc.element[b] = el[b].data(); nc.mass[b] = mass[b].data(); nc.temperature[b] = temperature[b].data();
            }
            ma::oni::colony::write_shadow(nc);
            const auto warm = static_cast<std::size_t>(native_cell(4, 4)), drawn = static_cast<std::size_t>(native_cell(5, 5));
            require(el[1][warm] != vacuum && mass[1][warm] > 0 && mass[1][drawn] > .5f, "shadow holds the owner's cells");
            const double expected = mass[1][warm] * (el[1][warm] == oxygen ? 1000. : 850.) * 2;
            const auto emptied = static_cast<std::size_t>(native_cell(14, 7));
            const auto drawn_mix = mix(5, 5), emptied_mix = mix(14, 7), totals = gas_totals();
            require(drawn_mix[0] > 0 && drawn_mix[1] > 0 && emptied_mix[0] > 0 && emptied_mix[1] > 0, "mixed gas cells for native takes");
            const float drawn_before = mass[1][drawn], emptied_shadow = mass[1][emptied];
            // The emptied cell keeps its element at no mass, as ONI leaves a
            // cell whose last gas it deleted.
            temperature[1][warm] += 2; mass[1][drawn] -= .1f; mass[1][emptied] = 0;
            const double taken = static_cast<double>(drawn_before) - mass[1][drawn];
            // Native froze a pool cell and melted a rock cell beside the gas.
            const auto frozen = static_cast<std::size_t>(native_cell(3, 1)), melted = static_cast<std::size_t>(native_cell(0, 4));
            require(el[1][frozen] == water && el[1][melted] == 0, "freeze and melt cells");
            const double frozen_mass = mass[1][frozen];
            el[1][frozen] = 1; el[1][melted] = water; mass[1][melted] = 500; temperature[1][melted] = 280;
            const auto before = ma::oni::colony::status();
            ma::oni::colony::capture_shadow(nc);
            ma::oni::colony::advance(9, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::cout << "shadow heat " << after.native_heat_j << " J (expected " << expected << ") taken " << after.native_mass_out_kg
                      << " kg changes " << after.native_changes << '\n';
            if (after.state != 1) throw std::runtime_error(std::string("shadow faulted: ") + after.error);
            // The take splits over the cell's gases (with the pool's vapour) by
            // mass; the emptied cell's minority gas leaves with its bulk.
            const auto left = gas_totals(); const double share = taken / (drawn_mix[0] + drawn_mix[1] + drawn_mix[2]);
            for (int q = 0; q < 2; ++q)
                require(std::abs(left[q] - (totals[q] - share * drawn_mix[q] - emptied_mix[q])) < 1e-9 * totals[q], "native takes leave as gas mixtures");
            const auto residue = mix(14, 7);
            require(residue[0] == 0 && residue[1] == 0 && residue[2] == 0, "an emptied cell keeps no gas");
            const double emptied_gas = emptied_mix[0] + emptied_mix[1] + emptied_mix[2];
            require(after.native_changes == 5 && std::abs(after.native_heat_j - expected) < 1e-6 * expected &&
                std::abs(after.native_mass_out_kg - taken - frozen_mass - emptied_shadow) < 1e-3 && after.committed_heat == before.committed_heat + 1 &&
                std::abs(after.extracted_kg - before.extracted_kg - taken - frozen_mass - emptied_gas) < 1e-3, "shadow changes are booked");
            require(after.closed_cells == before.closed_cells + 1 && after.opened_cells == before.opened_cells + 1 &&
                std::abs(after.injected_kg - before.injected_kg - 500) < 1e-3, "frozen cell closed, melted cell opened with its water");
            require(!suppress(MA_ONI_PRESSURE, native_cell(3, 1)) && suppress(MA_ONI_PRESSURE, native_cell(0, 4)), "ownership follows freeze and melt");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "shadow ledgers close");
            // Taking a sealed cell's last oxygen takes the rounding-sized
            // carbon dioxide trace beside it too.
            const MAOniColonyTransfer fill[] = {{public_cell(14, 7), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 0, .5, 300, 0},
                                                {public_cell(14, 7), carbon, MA_ONI_COLONY_INJECT, 0, -1, 0, 1e-12, 300, 0}};
            ma::oni::colony::queue_transfers(fill); ma::oni::colony::advance(10, .2, 2); ma::oni::colony::wait();
            const auto filled = mix(14, 7);
            require(filled[0] > .4 && filled[1] > 0, "sealed cell holds oxygen and a carbon dioxide trace");
            const MAOniColonyTransfer breath{public_cell(14, 7), oxygen, MA_ONI_COLONY_EXTRACT, 0, -1, 0, 1, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&breath, 1)); ma::oni::colony::advance(11, .2, 2); ma::oni::colony::wait();
            const auto drained = mix(14, 7); const auto end = ma::oni::colony::status();
            require(end.state == 1 && drained[0] == 0 && drained[1] == 0 && drained[2] == 0, "the trace leaves with the last oxygen");
            require(end.maximum_relative_mass_error < 1e-12 && end.maximum_relative_energy_error < 1e-9, "trace ledgers close");
            // A queued close leaves the cell to ONI at once: the shadow keeps
            // the solid that replaced the gas. A solid gone again before the
            // owner closes the cell (falling sand) cancels the close.
            const auto falling = static_cast<std::size_t>(native_cell(10, 4));
            const MAOniColonyTerrain close{public_cell(10, 4), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::queue_terrain(std::span(&close, 1));
            require(!ma::oni::colony::owns(native_cell(10, 4)) && !suppress(MA_ONI_PRESSURE, native_cell(10, 4)), "a closing cell is ONI's");
            for (int b = 0; b < 2; ++b) { el[b][falling] = 1; mass[b][falling] = 500; temperature[b][falling] = 300; }
            ma::oni::colony::write_shadow(nc);
            require(el[1][falling] == 1 && mass[1][falling] == 500, "the shadow keeps the solid in a closing cell");
            el[1][falling] = oxygen; mass[1][falling] = 1.2f;
            const auto held = ma::oni::colony::status();
            ma::oni::colony::capture_shadow(nc);
            require(ma::oni::colony::owns(native_cell(10, 4)), "the solid's departure cancels the close");
            ma::oni::colony::advance(12, .2, 2); ma::oni::colony::wait();
            const auto after_fall = ma::oni::colony::status();
            require(after_fall.state == 1 && after_fall.closed_cells == held.closed_cells && after_fall.injected_kg == held.injected_kg,
                "nothing closed or injected for the cancelled close");
            // A solid placed after its close was queued and melted within the
            // same native update: the owner closes the cell while ONI holds the
            // melt water; the next shadow reopens the cell with it, once.
            const auto melt = static_cast<std::size_t>(native_cell(8, 6));
            const MAOniColonyTerrain placed{public_cell(8, 6), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::queue_terrain(std::span(&placed, 1));
            ma::oni::colony::write_shadow(nc);
            el[1][melt] = water; mass[1][melt] = 400; temperature[1][melt] = 280;
            ma::oni::colony::capture_shadow(nc);
            ma::oni::colony::advance(13, .2, 2); ma::oni::colony::wait();
            const auto closed_over = ma::oni::colony::status();
            require(closed_over.state == 1 && closed_over.closed_cells == after_fall.closed_cells + 1 && !ma::oni::colony::owns(native_cell(8, 6)),
                "the close applied over ONI's melt water");
            for (int b = 0; b < 2; ++b) { el[b][melt] = water; mass[b][melt] = 400; temperature[b][melt] = 280; }
            for (int pass = 0; pass < 2; ++pass) { ma::oni::colony::write_shadow(nc); ma::oni::colony::capture_shadow(nc); }
            ma::oni::colony::advance(14, .2, 2); ma::oni::colony::wait();
            const auto reopened = ma::oni::colony::status();
            require(reopened.state == 1 && ma::oni::colony::owns(native_cell(8, 6)) && reopened.opened_cells == closed_over.opened_cells + 1 &&
                std::abs(reopened.injected_kg - closed_over.injected_kg - 400) < 1e-3, "the cell reopens with ONI's melt water, once");
            require(reopened.maximum_relative_mass_error < 1e-12 && reopened.maximum_relative_energy_error < 1e-9, "reopened ledgers close");
        }
        // A different catalogue rejects the image.
        ma::oni::colony::stop();
        auto altered = materials; altered[0].specific_heat_j_per_kg_k = 1001;
        bool rejected = false;
        try { ma::oni::colony::restore(config, altered, image, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && !ma::oni::colony::active(), "catalogue mismatch rejects");
        auto corrupt = image; corrupt[corrupt.size() / 2] ^= 1; rejected = false;
        try { ma::oni::colony::restore(config, materials, corrupt, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "corrupt image rejects");
        // Inputs in flight at a save: an injection survives the load without
        // its callback; an extraction answering a callback is asked again.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer queued[] = {{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, 42, 0, .5, 300, 0},
                                                  {public_cell(10, 5), oxygen, MA_ONI_COLONY_EXTRACT, 1, 43, 0, .3, 0, 0}};
            ma::oni::colony::queue_transfers(queued);
            const auto saved = ma::oni::colony::checkpoint();
            ma::oni::colony::stop(); ma::oni::colony::restore(config, materials, saved, 1);
            const auto before = ma::oni::colony::status();
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            require(after.state == 1 && std::abs(after.injected_kg - before.injected_kg - .5) < 1e-6 && after.extracted_kg == before.extracted_kg && n == 0,
                "queued inputs survive a save without callbacks");
            ma::oni::colony::stop();
        }
        // Hot salt water converts to steam over steps and its salt leaves as debris.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer hot{public_cell(6, 6), brine, MA_ONI_COLONY_INJECT, 0, -1, 0, 20, 420, 0};
            ma::oni::colony::queue_transfers(std::span(&hot, 1));
            for (std::uint64_t frame = 5; frame <= 7; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            double salt = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].kind == MA_ONI_COLONY_BYPRODUCT && got[q].callback == 777) salt += got[q].mass_kg;
            std::cout << "converted " << after.converted_kg << " kg, salt " << salt << " kg state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && after.converted_kg > 1 && std::abs(salt - .07 * after.converted_kg) < 1e-3 * after.converted_kg, "salt water boils to steam and salt");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "conversion ledgers close");
            ma::oni::colony::stop();
        }
        // Cold water the projection hides (a puddle on rock under gas) freezes
        // into its solid as debris, booked like a conversion byproduct.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer cold{public_cell(11, 6), water, MA_ONI_COLONY_INJECT, 0, -1, 0, 5, 150, 0};
            ma::oni::colony::queue_transfers(std::span(&cold, 1));
            for (std::uint64_t frame = 5; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            double ice = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].kind == MA_ONI_COLONY_BYPRODUCT && got[q].callback == 99) ice += got[q].mass_kg;
            std::cout << "frozen " << ice << " kg of 5 kg\n";
            require(after.state == 1 && ice > 2 && ice <= 5.0001, "hidden cold water freezes into debris");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "freezing ledgers close");
            ma::oni::colony::stop();
        }
        // A liquid trace below binary64's normal range (a rounding product
        // of the kernel) leaves the cell's reading as it was.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const double plain = ma::oni::colony::trace_reading_temperature(public_cell(4, 7), water, 0);
            const double traced = ma::oni::colony::trace_reading_temperature(public_cell(4, 7), water, 1e-321);
            std::cout << "subnormal water trace reads " << traced << " K against " << plain << " K\n";
            require(traced == plain, "a subnormal liquid trace keeps the cell readable");
            ma::oni::colony::stop();
        }
        // A cell whose reading the reference EOS refuses keeps its previous
        // view for the frame instead of faulting the owner.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyCellView> before(cells.size()), after(cells.size()); MAOniColonyStatus shown{};
            ma::oni::colony::copy(shown, before.data(), static_cast<std::uint32_t>(before.size()));
            ma::oni::colony::refuse_reading(public_cell(4, 7));
            ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
            ma::oni::colony::copy(shown, after.data(), static_cast<std::uint32_t>(after.size()));
            std::vector<MAOniColonyReading> refused_readings; std::vector<MAOniColonyConstituent> refused_parts;
            ma::oni::colony::readings(refused_readings, refused_parts);
            ma::oni::colony::refuse_reading(-1);
            const std::size_t k = 4 + 7 * W;
            std::cout << "refused reading: state " << shown.state << " " << shown.error << "\n";
            require(shown.state == 1 && std::memcmp(&before[k], &after[k], sizeof(MAOniColonyCellView)) == 0, "a refused reading keeps the cell's previous view");
            // The presentation's readings come from the projection's evaluation:
            // each matches its cell's view, and the refused cell has none.
            std::size_t matched = 0;
            for (const auto& r : refused_readings) {
                require(r.public_cell != public_cell(4, 7), "a refused cell has no presentation reading");
                const auto& v = after[(r.public_cell % GW - X0) + (r.public_cell / GW - Y0) * W];
                require(static_cast<float>(std::clamp(r.temperature_k, 1.0, 9999.0)) == v.temperature_k &&
                    static_cast<float>(r.mechanical_pressure_pa) == v.pressure_pa, "a reading matches its cell's view");
                ++matched;
            }
            std::cout << "readings matching their views: " << matched << "\n";
            require(matched > 0, "the owner publishes readings");
            ma::oni::colony::stop();
        }
        // An injection too small to change its cell in binary64 (1e-14 kg on
        // a tonne of water) is dropped with a zero receipt instead of faulting.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer speck{public_cell(3, 1), water, MA_ONI_COLONY_INJECT, 0, 55, 0, 1e-14, 300, 0};
            ma::oni::colony::queue_transfers(std::span(&speck, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto after = ma::oni::colony::status();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            bool zero = false; for (std::uint32_t q = 0; q < n; ++q) zero = zero || (got[q].callback == 55 && got[q].mass_kg == 0);
            std::cout << "unrepresentable injection: state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && zero && after.maximum_relative_mass_error < 1e-12, "an unrepresentable injection is dropped with a zero receipt");
            ma::oni::colony::stop();
        }
        // A terrain edit the owner cannot apply waits for the next frame
        // while the others in its batch apply.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const auto start = ma::oni::colony::status();
            const MAOniColonyTerrain tiles[] = {{public_cell(9, 6), MA_ONI_COLONY_CLOSE}, {public_cell(12, 6), MA_ONI_COLONY_CLOSE}};
            ma::oni::colony::refuse_terrain(public_cell(12, 6));
            ma::oni::colony::queue_terrain(tiles);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto refused = ma::oni::colony::status();
            ma::oni::colony::refuse_terrain(-1);
            ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
            const auto later = ma::oni::colony::status();
            std::cout << "refused terrain: closed " << refused.closed_cells - start.closed_cells << " then " << later.closed_cells - start.closed_cells << ", state " << later.state << " " << later.error << "\n";
            require(refused.state == 1 && refused.closed_cells == start.closed_cells + 1 && later.state == 1 && later.closed_cells == start.closed_cells + 2 &&
                later.maximum_relative_mass_error < 1e-12, "a refused terrain edit waits a frame, the rest of its batch applies");
            // A close whose displacement is refused sends the cell's contents
            // to the reservoir instead of waiting forever.
            const MAOniColonyTerrain tile{public_cell(10, 6), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::refuse_displacement(public_cell(10, 6));
            ma::oni::colony::queue_terrain(std::span(&tile, 1));
            ma::oni::colony::advance(7, .2, 2); ma::oni::colony::wait();
            ma::oni::colony::refuse_displacement(-1);
            const auto drained = ma::oni::colony::status();
            // (Gas leaves the reservoir again at once, into the open cells near it.)
            std::cout << "refused displacement: closed " << drained.closed_cells - later.closed_cells << ", through the reservoir "
                      << drained.released_kg - later.released_kg + drained.reservoir_kg - later.reservoir_kg << " kg\n";
            require(drained.state == 1 && drained.closed_cells == later.closed_cells + 1 &&
                drained.released_kg - later.released_kg + drained.reservoir_kg - later.reservoir_kg > .5 &&
                drained.maximum_relative_mass_error < 1e-12 && drained.maximum_relative_energy_error < 1e-9, "a close whose displacement is refused drains to the reservoir");
            ma::oni::colony::stop();
        }
        // A cell that refuses its heat passes it to an open neighbour: the
        // energy still reaches the owner and the ledger stays closed.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const auto before = ma::oni::colony::status();
            const MAOniColonyHeat warm{public_cell(4, 7), 0, 500, 0};
            ma::oni::colony::refuse_heat(public_cell(4, 7));
            ma::oni::colony::queue_heat(std::span(&warm, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            ma::oni::colony::refuse_heat(-1);
            const auto after = ma::oni::colony::status();
            std::cout << "refused heat: committed " << after.committed_heat - before.committed_heat << ", state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && after.committed_heat == before.committed_heat + 1 && after.maximum_relative_energy_error < 1e-9,
                "a refused heat edit goes to an open neighbour");
            ma::oni::colony::stop();
        }
        // A phase relaxation the reference EOS refuses skips the frame.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const auto before = ma::oni::colony::status();
            ma::oni::colony::refuse_relaxation(true);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            ma::oni::colony::refuse_relaxation(false);
            const auto after = ma::oni::colony::status();
            require(after.state == 1 && after.skipped_relaxations == before.skipped_relaxations + 1, "a refused phase relaxation skips its frame");
            ma::oni::colony::stop();
        }
        // A heat edit the cell cannot take (here more cooling than its gas
        // holds) stays queued, unbooked, instead of faulting the owner.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyHeat cold{public_cell(4, 7), 0, -1e12, 0};
            ma::oni::colony::queue_heat(std::span(&cold, 1));
            for (std::uint64_t frame = 5; frame <= 6; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::cout << "impossible heat edit: state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && after.maximum_relative_energy_error < 1e-9, "an untakeable heat edit is deferred, not faulted");
            ma::oni::colony::stop();
        }
        // A sealed cell's gas goes to the reservoir and seeps back when the
        // cell reopens.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTerrain walls[] = {{public_cell(13, 7), MA_ONI_COLONY_CLOSE}, {public_cell(14, 6), MA_ONI_COLONY_CLOSE}};
            ma::oni::colony::queue_terrain(walls);
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const MAOniColonyTerrain seal{public_cell(14, 7), MA_ONI_COLONY_CLOSE};
            ma::oni::colony::queue_terrain(std::span(&seal, 1));
            ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
            const auto sealed = ma::oni::colony::status();
            require(sealed.state == 1 && sealed.reservoir_kg > 1, "sealed gas went to the reservoir");
            const MAOniColonyTerrain dig{public_cell(14, 7), MA_ONI_COLONY_OPEN};
            ma::oni::colony::queue_terrain(std::span(&dig, 1));
            ma::oni::colony::advance(7, .2, 2); ma::oni::colony::wait();
            const auto reopened = ma::oni::colony::status();
            std::cout << "reservoir " << sealed.reservoir_kg << " kg, released " << reopened.released_kg << " kg\n";
            require(reopened.state == 1 && reopened.released_kg > 1 && reopened.reservoir_kg < 1e-6, "the reservoir seeps back");
            require(reopened.maximum_relative_mass_error < 1e-12 && reopened.maximum_relative_energy_error < 1e-9, "reservoir ledgers close");
            ma::oni::colony::stop();
        }
        // A pump takes the gas mixture around it: one receipt per constituent.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            const MAOniColonyTransfer pump{public_cell(9, 5), 0, MA_ONI_COLONY_EXTRACT_GAS, 2, 11, 0, 2, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&pump, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyReceipt> got(8); const auto n = ma::oni::colony::copy_receipts(got.data(), 8);
            double total = 0; bool oxygen_seen = false, carbon_seen = false;
            for (std::uint32_t q = 0; q < n; ++q) if (got[q].callback == 11) {
                total += got[q].mass_kg; oxygen_seen |= got[q].native_index == oxygen; carbon_seen |= got[q].native_index == carbon;
                require(got[q].kind == MA_ONI_COLONY_EXTRACT_GAS && got[q].temperature_k > 250, "mixture receipt");
            }
            std::cout << "pumped " << total << " kg in " << n << " receipts\n";
            require(oxygen_seen && carbon_seen && std::abs(total - 2) < 1e-5, "the pump takes the mixture");
            require(ma::oni::colony::status().maximum_relative_mass_error < 1e-12, "pump ledger closes");
            ma::oni::colony::stop();
        }
        // ONI's element replacement with a fluid empties the cell first.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            std::uint32_t idx[8]; double m[8]; double held = 0;
            const auto n = ma::oni::colony::composition(public_cell(9, 6), idx, m, 8);
            for (std::uint32_t q = 0; q < n; ++q) held += m[q];
            const auto before = ma::oni::colony::status().extracted_kg;
            const MAOniColonyTerrain clear{public_cell(9, 6), MA_ONI_COLONY_CLEAR};
            ma::oni::colony::queue_terrain(std::span(&clear, 1));
            ma::oni::colony::advance(5, .2, 2); ma::oni::colony::wait();
            const auto cs = ma::oni::colony::status();
            std::cout << "cleared " << cs.extracted_kg - before << " kg of the cell's " << held << " kg\n";
            require(cs.state == 1 && std::abs(cs.extracted_kg - before - held) <= 1e-9 * held && cs.maximum_relative_mass_error < 1e-12,
                "a replaced cell loses its fluid");
            ma::oni::colony::stop();
        }
        // Germs injected with gas travel with it and leave with extracted mass.
        {
            ma::oni::colony::restore(config, materials, image, 1);
            MAOniColonyTransfer sneeze{public_cell(10, 5), oxygen, MA_ONI_COLONY_INJECT, 0, -1, 3, .5, 300, 1e6};
            ma::oni::colony::queue_transfers(std::span(&sneeze, 1));
            for (std::uint64_t frame = 5; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            std::vector<MAOniColonyCellView> germ_views(W * H); MAOniColonyStatus gs{};
            ma::oni::colony::copy(gs, germ_views.data(), static_cast<std::uint32_t>(germ_views.size()));
            double total = 0; std::size_t holding = 0;
            for (const auto& v : germ_views) if (v.disease == 3) { total += v.disease_count; ++holding; }
            const MAOniColonyTransfer breath{public_cell(10, 5), oxygen, MA_ONI_COLONY_EXTRACT, 1, 77, 0, .05, 0, 0};
            ma::oni::colony::queue_transfers(std::span(&breath, 1));
            ma::oni::colony::advance(9, .2, 2); ma::oni::colony::wait();
            std::vector<MAOniColonyReceipt> got(16); const auto n = ma::oni::colony::copy_receipts(got.data(), 16);
            std::int32_t breathed = 0; for (std::uint32_t q = 0; q < n; ++q) if (got[q].callback == 77 && got[q].disease == 3) breathed += got[q].disease_count;
            std::cout << "germs " << total << " in " << holding << " cells, breathed " << breathed << '\n';
            require(gs.state == 1 && holding > 1 && std::abs(total - 1e6) <= 1e6 * 1e-3 && breathed > 0, "germs travel with gas and leave with it");
            ma::oni::colony::stop();
        }
        // An airflow tile column between a pool under air and thinner air
        // passes the gas but no liquid. The column is rock at import and
        // opens as tiles on the first frame.
        {
            auto small = config; small.width = 10; small.height = 8;
            std::vector<MAOniColonyCell> chamber(10 * 8);
            for (std::uint32_t y = 0; y < 8; ++y) for (std::uint32_t x = 0; x < 10; ++x) {
                auto& c = chamber[x + y * 10];
                if (x == 0 || x == 9 || y == 0 || y == 7 || x == 4) c = {0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0};
                else if (x < 4 && y <= 3) c = {water, MA_ONI_COLONY_FLUID, 1000, 290, 0, 0};
                else c = {oxygen, MA_ONI_COLONY_FLUID, x < 4 ? 1.2f : .4f, 290, 0, 0};
            }
            ma::oni::colony::start(small, materials, chamber, 1);
            const auto at = [&](std::uint32_t x, std::uint32_t y) { return (X0 + x) + (Y0 + y) * GW; };
            std::vector<MAOniColonyTerrain> tiles;
            for (std::uint32_t y = 1; y <= 6; ++y) { tiles.push_back({at(4, y), MA_ONI_COLONY_OPEN}); tiles.push_back({at(4, y), MA_ONI_COLONY_BLOCK_LIQUID}); }
            ma::oni::colony::queue_terrain(tiles);
            const auto right = [&](bool liquid, std::uint32_t from) {
                double total = 0; std::uint32_t idx[8]; double m[8];
                for (std::uint32_t y = 1; y <= 6; ++y) for (std::uint32_t x = from; x <= 8; ++x) {
                    const auto n = ma::oni::colony::composition(at(x, y), idx, m, 8);
                    for (std::uint32_t q = 0; q < n; ++q) if ((idx[q] == water) == liquid) total += m[q];
                }
                return total;
            };
            const double gas_before = right(false, 5);
            for (std::uint64_t frame = 1; frame <= 8; ++frame) { ma::oni::colony::advance(frame, .2, 2); ma::oni::colony::wait(); }
            const auto after = ma::oni::colony::status();
            std::cout << "airflow tiles: liquid in or beyond " << right(true, 4) << " kg, gas beyond " << gas_before << " -> " << right(false, 5)
                      << " kg, state " << after.state << " " << after.error << "\n";
            require(after.state == 1 && right(true, 4) == 0, "liquid crossed an airflow tile");
            require(right(false, 5) > gas_before + .5, "gas did not pass the airflow tiles");
            require(after.maximum_relative_mass_error < 1e-12 && after.maximum_relative_energy_error < 1e-9, "airflow tile ledgers close");
            ma::oni::colony::stop();
        }
        // Frames posted while a step runs join the next step: no game time is lost.
        ma::oni::colony::restore(config, materials, image, 1);
        ma::oni::colony::advance(5, .2, 2); ma::oni::colony::advance(6, .2, 2); ma::oni::colony::wait();
        const auto merged = ma::oni::colony::status();
        require(merged.state == 1 && merged.native_ticket == 6 && std::abs(merged.time_seconds - s.time_seconds) < 1e-9, "queued frames keep game time");
        // A frame from the game thread faults the owner.
        ma::oni::colony::advance(7, .2, 1); ma::oni::colony::wait();
        require(ma::oni::colony::status().state == 2, "game-thread frame refused");
        ma::oni::colony::stop();
        std::cout << "COLONY PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "COLONY FAIL: " << e.what() << '\n';
        return 1;
    }
}
