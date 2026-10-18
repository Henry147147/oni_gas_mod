#include "mac_fixture.hpp"
#include <bit>
#include <iomanip>

namespace {
using namespace ma;
using namespace ma::experimental;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
SpeciesRegistry catalogue(std::size_t count) {
    SpeciesRegistry result;const auto standard=SpeciesRegistry::standard();auto gas=standard[standard.index("nitrogen")];
    for(std::size_t i=0;i<count;++i){gas.id="species_"+std::to_string(i);gas.name=gas.id;result.add(gas);}
    return result;
}
MacTransportState state(const SpeciesRegistry& registry,std::size_t width,std::size_t height,bool all=false) {
    MacTransportState result;result.grid={width,height,1,1,true,true,std::vector<std::uint8_t>(width*height)};
    result.cells.reserve(width*height);
    for(std::size_t i=0;i<width*height;++i) {
        MacMaterialCell c{MixtureInventory(registry.size()),{}};c.mixture.masses[0]=1;
        if(all)for(std::size_t s=1;s<registry.size();++s)c.mixture.masses[s]=1e-30;
        else c.mixture.masses.back()=1e-28;
        c.mixture.set_temperature(registry,300);c.passive_quantities={{17,1e-30}};result.cells.push_back(std::move(c));
    }
    result.momentum_x.assign(width*height,.125);result.momentum_y.assign(width*height,0);return result;
}
std::uint64_t fingerprint(const MacTransportState& source) {
    std::uint64_t hash=UINT64_C(14695981039346656037);
    const auto add=[&](double x){hash^=std::bit_cast<std::uint64_t>(x);hash*=UINT64_C(1099511628211);};
    for(const auto& c:source.cells){for(const auto x:c.mixture.masses)add(x);add(c.mixture.energy);for(const auto& p:c.passive_quantities)add(p.quantity);}
    for(const auto x:source.momentum_x)add(x);
    for(const auto y:source.momentum_y)add(y);
    return hash;
}
std::shared_ptr<const FluidThermodynamics> model_for(const SpeciesRegistry& registry) {
    std::vector<ReferenceMaterial> definitions;
    for(const auto s:{std::size_t(0),registry.size()-1}) {
        ReferenceMaterial m;m.id=registry[s].id;m.phase=ReferencePhase::ideal_gas;m.molar_mass_kg_per_mol=registry[s].molar_mass;
        m.cv_j_per_kg_k=registry[s].cv();m.energy_offset_j_per_kg=registry[s].internal_energy(0);m.minimum_temperature_k=1;m.maximum_temperature_k=1e6;
        m.provenance="Explicit ideal-gas slot-capacity test, not a game material model";definitions.push_back(m);
    }
    return std::make_shared<const FluidThermodynamics>(registry,"sparse-catalogue-control-v1",std::move(definitions));
}
void large_catalogue() {
    const auto registry=catalogue(4097);const auto source=state(registry,64,64);const auto held=fingerprint(source);
    MacMassFluxes flux{std::vector<double>(4096,.25),std::vector<double>(4096)};
    const auto next=advance_mac_transport(registry,source,flux);
    check(next.report.constituent_accumulators==8192,"Sparse workspace allocated absent catalogue constituents");
    check(fingerprint(source)==held,"Sparse transport mutated its large source");
    for(std::size_t i=0;i<source.cells.size();++i) {
        check(next.state.cells[i].mixture.masses==source.cells[i].mixture.masses && next.state.cells[i].mixture.energy==source.cells[i].mixture.energy &&
            next.state.cells[i].passive_quantities[0].quantity==source.cells[i].passive_quantities[0].quantity,
            "Large sparse-catalogue identity lost a constituent, U or passive trace");
    }
    check(std::abs(next.report.energy_residual_j)<=next.report.energy_bound_j,"Large sparse-catalogue energy account failed");
    std::cout<<"SPARSE "<<registry.size()<<' '<<source.cells.size()<<' '<<next.report.constituent_accumulators<<' '
        <<registry.size()*source.cells.size()<<' '<<sizeof(MacScaledSum)<<'\n';
    std::cout<<"PASS 4097-species, 4096-cell nonzero-flux transport beyond the old 16-million-slot cap\n";
    const auto model=model_for(registry);const std::vector<double> volumes(4096,1);
    const auto plan=reconstruct_mac_swept_parcels(registry,*model,source,volumes,flux);
    const auto parcels=advance_mac_parcel_transport(registry,source,*model,plan);
    check(plan.parcels.size()==4096 && parcels.report.constituent_accumulators==8192 && fingerprint(source)==held,
        "Large-catalogue reconstructed parcels retained the old product cap or changed source");
    for(std::size_t i=0;i<4096;++i)check(parcels.state.cells[i].mixture.masses==source.cells[i].mixture.masses &&
        parcels.state.cells[i].mixture.energy==source.cells[i].mixture.energy,"Large EOS reconstruction changed a primary inventory");
    std::cout<<"PASS EOS reconstruction and actual parcels beyond the old catalogue-product cap\n";
}
void all_present_and_union() {
    const auto registry=catalogue(4097);auto source=state(registry,2,2,true);
    MacMassFluxes flux{std::vector<double>(4,.125),std::vector<double>(4,.25)};
    const auto all=advance_mac_transport(registry,source,flux);
    check(all.report.constituent_accumulators==4*registry.size(),"Sparse workspace capped simultaneous constituents");
    for(const auto& c:all.state.cells)for(std::size_t s=1;s<registry.size();++s)
        check(c.mixture.masses[s]==1e-30,"Simultaneously present constituent was dropped or changed");
    source=state(registry,2,2);
    for(std::size_t i=0;i<4;++i){source.cells[i].mixture.masses.back()=0;source.cells[i].mixture.masses[registry.size()-4+i]=1e-28;source.cells[i].mixture.set_temperature(registry,300);}
    const auto held=fingerprint(source);const auto next=advance_mac_transport(registry,source,flux);
    check(next.report.constituent_accumulators==16 && fingerprint(source)==held,"Sparse donor union coverage or immutability differs");
    for(std::size_t i=0;i<4;++i)for(std::size_t donor=0;donor<4;++donor) {
        const auto left=(i%2+1)%2+(i/2)*2,below=i%2+((i/2+1)%2)*2;
        const double fraction=donor==i?.625:donor==left?.125:donor==below?.25:0;
        const auto actual=next.state.cells[i].mixture.masses[registry.size()-4+donor];
        check(actual==fraction*1e-28,"Sparse rows failed the analytic two-axis constituent transfer");
    }
    std::cout<<"PASS every catalogue constituent simultaneously active, ordered incoming unions and analytic trace transfers\n";
}
void eos_parcels() {
    const auto registry=catalogue(257);const auto model=model_for(registry);
    const auto source=state(registry,4,2);const auto held=fingerprint(source);
    MacMassFluxes sweeps{std::vector<double>(8,.125),std::vector<double>(8)};const std::vector<double> volumes(8,1);
    const auto plan=reconstruct_mac_swept_parcels(registry,*model,source,volumes,sweeps);
    const auto next=advance_mac_parcel_transport(registry,source,*model,plan,MacMomentumReconstruction::swept_energy_flux);
    check(next.report.constituent_accumulators==16 && fingerprint(source)==held,"EOS parcel sparse union changed its source or expanded absent IDs");
    for(std::size_t i=0;i<8;++i) {
        check(next.state.cells[i].mixture.masses==source.cells[i].mixture.masses && next.state.cells[i].mixture.energy==source.cells[i].mixture.energy,
            "Sparse EOS parcel identity changed a primary field");
        check(model->evaluate(next.state.cells[i].mixture,1).temperature_k>0,"Sparse EOS parcel left the supported branch");
    }
    auto absent=source;
    for(auto& c:absent.cells){c.mixture.masses.back()=0;c.mixture.set_temperature(registry,300);}
    const auto empty_trace=reconstruct_mac_swept_parcels(registry,*model,absent,volumes,sweeps);
    const auto unchanged=fingerprint(absent);
    for(const bool parcel:{false,true}) {
        auto malformed=empty_trace;
        auto& contents=parcel?malformed.parcels[0].contents:malformed.retained[0];
        contents.mixture.masses.back()=1e-28;
        bool rejected=false;
        try{(void)advance_mac_parcel_transport(registry,absent,*model,malformed);}
        catch(const FluidFatalError& e){rejected=e.code()==FluidFatalCode::numerical_precision;}
        check(rejected && fingerprint(absent)==unchanged,"Sparse donor validation admitted an invented positive trace or mutated its source");
    }
    std::cout<<"PASS catalogue-mapped EOS parcels with high-index positive trace and unchanged primary fields\n";
    std::cout<<"PASS invented positive traces reject atomically in both retained and outgoing inventories\n";
}
}
int main(){try{std::cout<<std::setprecision(17);large_catalogue();all_present_and_union();eos_parcels();return 0;}
catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
