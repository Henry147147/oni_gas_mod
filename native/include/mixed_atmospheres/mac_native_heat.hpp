#pragma once
#include "mac_closed_world.hpp"

namespace ma::experimental {
struct MacNativeHeatBody {
    float temperature_k{};
    // Element chunks supply an already rounded binary32 capacity. Solid cells
    // instead derive capacity from separate native mass and specific heat;
    // retain that product rather than imposing another binary32 rounding.
    double heat_capacity_kj_per_k{};
};
struct MacNativeHeatReport {
    double requested_heat_j{},body_heat_loss_j{},stored_fluid_heat_j{};
    double energy_residual_j{},energy_bound_j{},entropy_lower_bound_j_per_k{};
    std::size_t iterations{};
    bool unresolved{};
    MacPrimaryRoundingCounts primary_energy{};
};
struct MacNativeHeatExchange {
    MacClosedWorld world;
    float body_temperature_k{};
    MacNativeHeatReport report;
};
// One passive, constant-capacity native body coupled to one full-EOS fluid
// cell. A backward-Euler contact is rounded toward the old native binary32
// body temperature; only represented body heat is admitted into the fluid.
// An unrepresentable endpoint leaves both owners unchanged and is reported.
// Species, passives, canonical momentum and the physical clock are retained.
// The caller must stage the result before changing the native body endpoint.
[[nodiscard]] MacNativeHeatExchange exchange_mac_native_heat(const MacClosedWorld&,std::size_t cell,
    MacNativeHeatBody,double conductance_w_per_k,double seconds);
}
