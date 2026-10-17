#include "../src/oni_domain.hpp"
#include "mixed_atmospheres/mac_energy.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace ma::oni;
using ma::experimental::MacEnergyAccount;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
MAOniDomainStatus capture(std::vector<MAOniDomainCell>& cells) {
    MAOniDomainStatus state{};copy_domain(state,cells.data(),static_cast<std::uint32_t>(cells.size()));
    require(state.state==1,"Mixed native owner faulted");
    for (const auto& cell : cells) {
        require(cell.masses[0]>0 && cell.masses[1]>0 && cell.masses[2]>0 && cell.masses[3]>0 &&
            cell.liquid_fraction>0 && cell.liquid_fraction<1 && cell.pressure>0,"Mixed publication lost a phase or trace");
        const auto water=domain_species(3);const auto expected=cell.masses[3]/(water.reference_density*(1+cell.pressure/2.2e9));
        require(std::abs(cell.liquid_fraction-expected)<=2e-10*expected,"Mixed publication used nominal instead of compressed liquid volume");
        double gas_volume=0;
        for (const auto species : {0U,1U,2U,4U}) gas_volume+=cell.masses[species]/domain_species(species).molar_mass*8.31446261815324*cell.temperature/cell.pressure;
        require(std::abs(gas_volume+cell.liquid_fraction-1)<2e-10,"Mixed publication failed occupied volume closure");
    }
    return state;
}
}
int main() {
    try {
        for (const auto scenario : {4U,5U}) {
        const MAOniDomainConfig config{40,2,1,64,10,10,3,2,scenario,0};start_domain(config,1);
        std::vector<MAOniDomainCell> cells(6);auto state=capture(cells);const auto initial=cells;
        const auto held=snapshot_domain(state.session,0);
        require(advance_domain_snapshot(held,0)==held,"Mixed frame-zero checkpoint did not retain every physical byte");
        MAOniDomainThermalStatus thermal{};copy_domain_thermal_status(thermal);
        require(thermal.enabled==(scenario==5 ? 1U : 0U) && thermal.stages==0,"Mixed scenario changed its declared conduction setting");
        std::array<MAOniDomainTransfer,2> edits{{
            {64,2,state.session,0,650,3,1,1,21,0,.03125,310},
            {64,2,state.session,0,651,1,2,1,22,0,.015625,0}}};
        auto invalid=edits;invalid.back().mass_kg=-1;MAOniDomainTransferStatus transfers{};bool rejected=false;
        try { queue_domain_transfers(invalid.data(),2,transfers); } catch (const std::exception&) { rejected=true; }
        require(rejected && snapshot_domain(state.session,0)==held,"Late mixed batch rejection changed an earlier cell or command");
        queue_domain_transfers(edits.data(),2,transfers);require(transfers.pending_batches==1 && transfers.accepted_batches==1,"Mixed transfer batch was not reserved");
        advance_domain(1,.2F,2);state=capture(cells);
        copy_domain_thermal_status(thermal);
        if (scenario==5) require(thermal.stages==2*state.substeps && thermal.contacts>0 && thermal.iterations>0 &&
            thermal.redistributed_energy_j>0,"Mixed native worker omitted its nonlinear conductive half stages");
        std::array<MAOniDomainReceipt,8> receipts{};std::uint32_t count=0;
        copy_domain_receipts(state,cells.data(),6,receipts.data(),8,count);
        require(count==2 && receipts[0].species==3 && receipts[0].kind==1 && receipts[0].callback==21 && receipts[0].mass_kg==.03125 &&
            receipts[1].species==1 && receipts[1].kind==2 && receipts[1].callback==22 && receipts[1].mass_kg==.015625,
            "Mixed point boundary lost real phase/callback/mass identity");
        MacEnergyAccount energy;std::array<MacEnergyAccount,5> mass;
        for (std::size_t cell=0;cell<cells.size();++cell) {
            energy.add(initial[cell].internal_energy);energy.add(initial[cell].resolved_kinetic_energy);
            energy.add(-cells[cell].internal_energy);energy.add(-cells[cell].resolved_kinetic_energy);
            for (std::size_t species=0;species<mass.size();++species) { mass[species].add(initial[cell].masses[species]);mass[species].add(-cells[cell].masses[species]); }
        }
        for (std::size_t i=0;i<count;++i) {
            const auto& receipt=receipts[i];const double sign=receipt.kind==1 ? 1 : -1;
            mass[receipt.species].add(sign*receipt.mass_kg);energy.add(sign*receipt.internal_energy_j);
            if (receipt.kind==2) energy.add(-receipt.kinetic_energy_j);
        }
        require(energy.within_bound(),"Mixed native worker failed complete U+K boundary accounting");
        for (const auto& value : mass) require(value.within_bound(),"Mixed native worker lost a constituent or trace");
        ack_domain_receipts(state.session,receipts[count-1].sequence);
        const auto image=snapshot_domain(state.session,state.completed_frames);const auto expected=advance_domain_snapshot(image,.2F);const auto time=state.time_seconds;
        validate_domain_checkpoint(config,image,time);shutdown_domain();start_domain(config,1,image,time);state=capture(cells);
        require(state.completed_frames==0 && state.time_seconds==time && snapshot_domain(state.session,0)==image,"Mixed native restore changed its frame-zero physical state");
        advance_domain(1,.2F,2);state=capture(cells);
        require(snapshot_domain(state.session,1)==expected,"Mixed native worker disagreed with complete checkpoint continuation");
        shutdown_domain();
        }
        std::cout << "PASS mixed native pressure transport with/without nonlinear conduction, EOS phase volumes, gas/liquid callbacks, trace/energy accounts, atomic rejection and exact restored continuation\n";return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
