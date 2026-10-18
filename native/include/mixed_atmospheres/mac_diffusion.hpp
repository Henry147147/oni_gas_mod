#pragma once
#include "mac_transport.hpp"
#include <memory>
#include <string>

namespace ma::experimental {
enum class MacDiffusionMode : std::uint32_t { disabled=0, physical=1, accelerated=2 };
struct MacDiffusionSettings {
    MacDiffusionMode mode=MacDiffusionMode::physical;
    double multiplier=1;
    void validate() const;
    static MacDiffusionSettings disabled() { return {MacDiffusionMode::disabled,1}; }
};
struct GasDiffusionPair {
    std::string first,second;
    // ln(D / cm^2 s^-1) = a + b/T + c ln(T), at 101325 Pa.
    double a,b,c,minimum_temperature_k,maximum_temperature_k;
    double minimum_pressure_pa,maximum_pressure_pa;
    bool estimated;
    std::string source;
};
// Value constructed and shared const: coefficients/provenance never mutate.
class GasTransportProfile {
public:
    GasTransportProfile(std::string identity,std::vector<GasDiffusionPair>);
    const std::string& identity() const { return identity_; }
    const std::vector<GasDiffusionPair>& pairs() const { return pairs_; }
    double diffusivity(std::string_view first,std::string_view second,double temperature,double pressure) const;
    static std::shared_ptr<const GasTransportProfile> standard();
private:
    std::string identity_;
    std::vector<GasDiffusionPair> pairs_;
};
struct MacDiffusionReport {
    std::size_t substeps=0,rejected_attempts=0,faces=0,unresolved_updates=0;
    double exchanged_mass_kg=0,elapsed_ms=0,entropy_change_j_per_k=0,entropy_roundoff_j_per_k=0;
    std::vector<double> species_mass_residual,species_mass_bound;
    MacEnergy energy_residual,energy_bound;
};
struct MacDiffusionAdvance { MacTransportState state; MacDiffusionReport report; };
// Occupied, gas-only finite volumes. No liquid/disease/passive diffusion.
// Heun predictor/corrector; coefficients follow each stage's actual T,p,X.
// Equal-and-opposite, mass-average-corrected face fluxes carry species enthalpy.
// Invalid candidates retry before publication. No clipping or trace cutoffs.
MacDiffusionAdvance advance_mac_diffusion(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double seconds,const MacDiffusionSettings&,
    const GasTransportProfile&,std::size_t attempt_budget=4096);
bool mac_diffusion_stationary(const SpeciesRegistry&,const MacTransportState&);
}
