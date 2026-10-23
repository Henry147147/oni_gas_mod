#pragma once
#include "oni_parcel_outcome.hpp"
#include "mixed_atmospheres/mac_closed_world.hpp"

namespace ma::oni::colony::detail {
// Preserve every represented operand. No sum.value() projection belongs at
// this boundary; the native owner and managed source have separate accounts.
inline void append_core_outcome(MovingParcelOutcome& output,
    const experimental::MacParcelDepositResult& result,
    std::span<const MovingParcelInput> requests) {
    output.refusal=static_cast<std::uint32_t>(result.refusal);
    output.reason=result.reason;
    output.energy_residual_j=result.energy_residual_j;output.energy_bound_j=result.energy_bound_j;
    output.momentum_x_residual=result.momentum_x_residual;output.momentum_x_bound=result.momentum_x_bound;
    output.momentum_y_residual=result.momentum_y_residual;output.momentum_y_bound=result.momentum_y_bound;
    if(!result.accepted())return;
    ParcelWireReader::require(result.receipts.size()==requests.size(),"Core moving receipt count differs");
    output.consumed.reserve(requests.size());output.grid.reserve(requests.size());
    for(std::size_t i=0;i<requests.size();++i) {
        const auto& receipt=result.receipts[i];const auto& source=receipt.consumed;
        MovingParcelInput consumed;consumed.public_cell=requests[i].public_cell;consumed.native_index=requests[i].native_index;
        consumed.mass_kg=source.mass_kg;consumed.internal_energy_j=source.internal_energy_j;
        consumed.kinetic_energy_j=source.kinetic_energy_j;consumed.potential_energy_j=source.potential_energy_j;
        consumed.momentum_x=source.momentum_x;consumed.momentum_y=source.momentum_y;consumed.passives=source.passives;
        output.consumed.push_back(std::move(consumed));
        ParcelGridCredit credit;credit.mass_kg=receipt.grid.mass_kg;credit.internal_energy_j=receipt.grid.internal_energy_j;
        credit.kinetic_energy_j=receipt.grid.kinetic_energy_j;credit.potential_energy_j=receipt.grid.potential_energy_j;
        credit.momentum_x=receipt.grid.momentum_x;credit.momentum_y=receipt.grid.momentum_y;
        credit.passives.reserve(receipt.grid.passives.size());
        for(const auto& passive : receipt.grid.passives)credit.passives.push_back({passive.identity,passive.quantity});
        credit.kinetic_mixing_heat_j=receipt.kinetic_mixing_heat_j;credit.potential_work_j=receipt.potential_work_j;
        credit.wall_impulse_x=receipt.wall_impulse_x;credit.wall_impulse_y=receipt.wall_impulse_y;
        credit.passives_before=receipt.passives_before;credit.passives_after=receipt.passives_after;
        output.grid.push_back(std::move(credit));
    }
}
}
