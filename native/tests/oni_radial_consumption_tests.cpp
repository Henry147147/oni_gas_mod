#include "../src/oni_domain.hpp"
#include "../src/oni_radial_consumption.hpp"
#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>

namespace {
using namespace ma::oni;using namespace ma::experimental;
void require(bool value,const char* why) {if (!value) throw std::runtime_error(why);}
template<class Action> void rejects(Action action) {
    bool rejected=false;try {action();} catch (const std::exception&) {rejected=true;}
    require(rejected,"Invalid radial consumption was accepted");
}
void inspect_plan(std::span<const double> source,double requested,const radial::Plan& plan) {
    MacEnergySum exact,available;std::set<std::size_t> cells;
    for (const auto amount:source) available.add(amount);
    for (const auto& part:plan.allocations) {
        require(part.cell<source.size() && cells.insert(part.cell).second && part.mass>0 && part.mass<=source[part.cell] &&
            static_cast<double>(static_cast<float>(part.mass))==part.mass,"Invalid radial parcel or callback lattice");exact.add(part.mass);
    }
    exact.add(-plan.mass);require(exact.sign()==0,"Radial callback differs from exact sum of physical parcels");
    require(plan.mass>=0 && plan.mass<=requested && static_cast<double>(static_cast<float>(plan.mass))==plan.mass,"Invalid aggregate callback mass");
    available.add(-plan.mass);require(available.sign()>=0,"Radial consumption exceeded source inventory");
}
void planner_controls() {
    const std::vector<std::uint8_t> walls(6);
    for (int exponent=-149;exponent<=100;++exponent) {
        const double request=static_cast<double>(std::ldexp(.75F,exponent));
        std::vector<double> source={request/7,request/11,request/13,request/17,request/19,request/23};
        const auto held=source;const auto plan=radial::prepare(3,2,walls,source,0,3,request);inspect_plan(source,request,plan);
        require(source==held,"Radial planner changed caller inventory");
    }
    const std::vector<double> values={.015625,.015625,.015625,.015625,.015625,.015625};
    const auto plan=radial::prepare(3,2,walls,values,0,3,.0625);inspect_plan(values,.0625,plan);
    require(plan.mass==.0625 && plan.allocations.size()==4 && plan.allocations[0].cell==0 && plan.allocations[1].cell==1 &&
        plan.allocations[2].cell==3 && plan.allocations[3].cell==2,"Radial centre-first connected order changed");
    auto trace=values;trace[0]=1e-28;const auto fine=radial::prepare(3,2,walls,trace,0,3,.0625);inspect_plan(trace,.0625,fine);
    require(std::none_of(fine.allocations.begin(),fine.allocations.end(),[](const auto& part) {return part.cell==0;}),"Sub-callback trace was removed or rounded up");
    const std::vector<std::uint8_t> barrier={0,1,0};const std::vector<double> separated={.015625,0,10};
    const auto stopped=radial::prepare(3,1,barrier,separated,0,255,1);inspect_plan(separated,1,stopped);
    require(stopped.mass==.015625 && stopped.allocations.size()==1,"Radial consumption crossed a solid boundary");
    for (const double invalid:{-.01,std::numeric_limits<double>::infinity(),.1})
        rejects([&] {(void)radial::prepare(3,2,walls,values,0,3,invalid);});
    rejects([&] {(void)radial::prepare(3,2,walls,values,0,256,1);});
    std::cout<<"PASS radial allocation across binary32 range, exact aggregate lattice, retained positive trace, connected order and solid barrier\n";
}
void rectangle_planner_controls() {
    std::vector<std::uint8_t> walls(12);std::vector<double> source(12,.03125);
    auto plan=radial::prepare_rectangle(4,3,walls,source,5,2,2,.09375);inspect_plan(source,.09375,plan);
    require(plan.allocations.size()==3 && plan.allocations[0].cell==5 && plan.allocations[1].cell==6 &&
        plan.allocations[2].cell==9,"Rectangle did not retain its lower-left anchor and row order");
    walls[6]=1;plan=radial::prepare_rectangle(4,3,walls,source,5,2,2,.09375);inspect_plan(source,.09375,plan);
    require(plan.allocations.size()==3 && plan.allocations[0].cell==5 && plan.allocations[1].cell==9 &&
        plan.allocations[2].cell==10,"A rectangular solid blocked unrelated selected cells or was consumed");
    for (int exponent=-149;exponent<=100;++exponent) {
        const double request=static_cast<double>(std::ldexp(.75F,exponent));
        for (std::size_t cell=0;cell<source.size();++cell) source[cell]=request/static_cast<double>(7+2*cell);
        const auto held=source;plan=radial::prepare_rectangle(4,3,walls,source,5,2,2,request);inspect_plan(source,request,plan);
        for (const auto& part:plan.allocations) require(part.cell==5 || part.cell==9 || part.cell==10,"Rectangle consumed an outside source");
        require(source==held,"Rectangular planner changed caller inventory");
    }
    const auto empty=radial::prepare_rectangle(4,3,walls,source,11,0,255,1);
    require(empty.mass==0 && empty.allocations.empty(),"Zero-width native rectangle did not retain empty consumption");
    rejects([&] {(void)radial::prepare_rectangle(4,3,walls,source,7,2,1,1);});
    rejects([&] {(void)radial::prepare_rectangle(4,3,walls,source,9,1,2,1);});
    rejects([&] {(void)radial::prepare_rectangle(4,3,walls,source,5,2,0,1);});
    std::cout<<"PASS rectangular footprint, row order, skipped solids, binary32 range, unchanged outside traces and empty area\n";
}
void owner_controls() {
    for (const auto scenario:{0U,4U}) for (const bool overlap:{false,true}) for (const bool rectangle:{false,true}) {
        const MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};std::array<MAOniDomainInitialCell,6> initial{};
        for (std::size_t i=0;i<initial.size();++i) {
            auto& cell=initial[i];cell.struct_size=56;cell.abi_version=1;cell.masses[0]=.5;cell.masses[1]=.015625;
            cell.masses[2]=1e-28;cell.masses[3]=scenario==4 ? 200 : 0;cell.temperature_k=280+10*static_cast<double>(i);
        }
        start_domain(config,1,{},0,initial);std::vector<MAOniDomainCell> before(6),after(6);MAOniDomainStatus status{};copy_domain(status,before.data(),6);
        const auto held=snapshot_domain(status.session,0);
        std::array<MAOniDomainTransfer,2> edits{{{64,2,status.session,0,650,1,2,1U|(3U<<8),71,0,.0625,0},
            {64,2,status.session,0,650,1,2,1U|(3U<<8),72,0,.0625,0}}};
        if (rectangle) for (auto& edit:edits) edit.flags=1U|(2U<<8)|(2U<<16);
        auto bad=edits;bad[1].temperature_k=std::numeric_limits<double>::quiet_NaN();MAOniDomainTransferStatus transfer{};
        std::memset(&transfer,0x5a,sizeof(transfer));const auto old_transfer=transfer;
        rejects([&] {queue_domain_transfers(bad.data(),2,transfer);});
        require(std::memcmp(&transfer,&old_transfer,sizeof(transfer))==0 && snapshot_domain(status.session,0)==held,"Late radial batch failure changed owner or output");
        queue_domain_transfers(edits.data(),overlap ? 2U : 1U,transfer);
        MAOniDomainStatus pending{};copy_domain(pending,after.data(),6);
        require(std::memcmp(before.data(),after.data(),6*sizeof(MAOniDomainCell))==0,"Radial reservation published before worker completion");
        advance_domain(1,.02F,2);std::array<MAOniDomainReceipt,4> receipts{};std::uint32_t count=0;
        copy_domain_receipts(status,after.data(),6,receipts.data(),4,count);
        require(status.state==1 && count==(overlap ? 2U : 1U),"Radial worker faulted or emitted one callback per source cell");
        require(receipts[0].kind==2 && receipts[0].callback==71 && receipts[0].mass_kg==.0625 && receipts[0].public_cell==650,
            "Radial aggregate lost callback, amount or centre");
        if (overlap) require(receipts[1].kind==2 && receipts[1].callback==72 && receipts[1].mass_kg==(rectangle ? 0 : .03125),"Overlapping regional requests spent the same source inventory");
        MacEnergyAccount energy,packet;std::array<MacEnergyAccount,5> mass;
        for (std::size_t i=0;i<6;++i) {
            energy.add(before[i].internal_energy);energy.add(before[i].resolved_kinetic_energy);
            energy.add(-after[i].internal_energy);energy.add(-after[i].resolved_kinetic_energy);
            for (std::size_t k=0;k<5;++k) {mass[k].add(before[i].masses[k]);mass[k].add(-after[i].masses[k]);}
        }
        const auto oxygen=domain_species(1);packet.add(receipts[0].internal_energy_j);
        const std::array<unsigned,4> selected=rectangle ? std::array<unsigned,4>{0,1,3,4} : std::array<unsigned,4>{0,1,3,2};
        for (const auto cell:selected) {packet.add(-.015625*oxygen.cv*before[cell].temperature);packet.add(-.015625*oxygen.energy_offset);}
        require(packet.within_bound() && std::abs(receipts[0].temperature_k-(rectangle ? 300 : 295))<1e-10,"Regional parcel heat or mass-weighted callback temperature changed");
        for (std::uint32_t i=0;i<count;++i) {
            energy.add(-receipts[i].internal_energy_j);energy.add(-receipts[i].kinetic_energy_j);mass[1].add(-receipts[i].mass_kg);
        }
        require(energy.within_bound(),"Radial consumption failed complete U+K boundary accounting");
        for (const auto& value:mass) require(value.within_bound(),"Radial consumption lost another constituent or positive trace");
        ack_domain_receipts(status.session,receipts[count-1].sequence);
        const auto image=snapshot_domain(status.session,status.completed_frames),expected=advance_domain_snapshot(image,.02F);const auto time=status.time_seconds;
        const auto journal=copy_domain_frame_inputs(status.session,status.completed_frames);
        require(advance_domain_snapshot_with_inputs(held,journal)==image,"Regional accepted-input journal did not replay its complete physical frame exactly");
        shutdown_domain();start_domain(config,1,image,time);
        MAOniDomainStatus restored{};copy_domain(restored,after.data(),6);
        require(snapshot_domain(restored.session,0)==image,"Radial source changed on zero-time restoration");
        advance_domain(1,.02F,2);copy_domain(restored,after.data(),6);
        require(snapshot_domain(restored.session,1)==expected,"Radial complete continuation differs from uninterrupted owner");shutdown_domain();
    }
    std::cout<<"PASS radial and rectangular gas/wet callbacks, overlapping atomic reservations, U+K/trace accounts, late rejection and exact input replay/continuation\n";
}
}
int main() {
    try {std::cout<<std::setprecision(17);planner_controls();rectangle_planner_controls();owner_controls();return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
