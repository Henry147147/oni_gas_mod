#pragma once
#include "mac_transport.hpp"
#include <memory>
#include <string>

namespace ma::experimental {
enum class MacDiffusionMode : std::uint32_t { disabled=0, physical=1, accelerated=2 };
// The persisted production kernel selects counted transfer rounding. The
// reference oracle retains its original strict storage/admission behaviour.
enum class MacDiffusionRounding { reference, counted };
// Discrete flux formulation stored in diffusion checkpoints. 2: Maxwell-Stefan
// driving force with pressure diffusion in the log-mean entropy-stable
// corrected mixture-averaged form, empty-cell faces closed, correlations
// extrapolated outside their validated ranges and combined with the Knudsen
// limit of the cell spacing (Bosanquet). Formulation 1 (mole-fraction gradient
// only, range-admitting) was never released and is rejected on restore.
constexpr std::uint32_t mac_diffusion_formulation=2;
struct MacDiffusionSettings {
    MacDiffusionMode mode=MacDiffusionMode::physical;
    // Accelerated mode scales the molecular binary coefficients. The Knudsen
    // (free-molecular) limit of the cell spacing is kinematic and is not scaled.
    double multiplier=1;
    void validate() const;
    static MacDiffusionSettings disabled() { return {MacDiffusionMode::disabled,1}; }
};
struct GasDiffusionPair {
    std::string first,second;
    // ln(D / cm^2 s^-1) = a + b/T + c ln(T), at 101325 Pa.
    double a,b,c;
    // Validated range of the correlation. States outside it are evaluated with
    // the same functional form (extrapolated) and counted; they do not reject.
    double minimum_temperature_k,maximum_temperature_k;
    double minimum_pressure_pa,maximum_pressure_pa;
    bool estimated;
    std::string source;
};
struct GasDiffusivity {
    double molecular_m2_per_s=0;  // correlation at (T,p), times the mode multiplier
    double knudsen_m2_per_s=0;    // (L/3) sqrt(8RT/(pi M_ij)), M_ij the pair reduced molar mass
    double effective_m2_per_s=0;  // Bosanquet: 1/D = 1/D_molecular + 1/D_Knudsen
    bool validated=true;          // false: T or p outside the pair's validated range
};
// Molar masses in kg/mol; spacing is the cell size L in metres.
[[nodiscard]] GasDiffusivity evaluate_gas_diffusivity(const GasDiffusionPair&,double temperature_k,double pressure_pa,
    double first_molar_mass,double second_molar_mass,double spacing_m,double multiplier=1);
// Value constructed and shared const: coefficients/provenance never mutate.
class GasTransportProfile {
public:
    GasTransportProfile(std::string identity,std::vector<GasDiffusionPair>);
    const std::string& identity() const { return identity_; }
    const std::vector<GasDiffusionPair>& pairs() const { return pairs_; }
    // Throws std::invalid_argument when the pair is absent.
    const GasDiffusionPair& pair(std::string_view first,std::string_view second) const;
    // Molecular correlation only (no multiplier or Knudsen limit), in its own
    // functional form at any positive finite T and p.
    double diffusivity(std::string_view first,std::string_view second,double temperature,double pressure) const;
    static std::shared_ptr<const GasTransportProfile> standard();
private:
    std::string identity_;
    std::vector<GasDiffusionPair> pairs_;
};
struct MacDiffusionReport {
    std::size_t substeps=0,rejected_attempts=0,faces=0,unresolved_updates=0;
    // Shared face transfers rounded once before either endpoint gathers them.
    // Each error is at most half the binary64 subnormal quantum. Counts and
    // full-range error magnitudes are audited for accepted stages only.
    std::size_t rounded_mass_transfers=0,rounded_energy_transfers=0;
    std::vector<std::size_t> species_rounded_transfers;
    std::vector<MacEnergy> species_transfer_roundoff;
    MacEnergy energy_transfer_roundoff;
    // (face, stage, pair) coefficient evaluations of accepted substeps whose
    // face T or p lies outside that pair's validated range, and the extreme
    // face T and p among them (all zero when there are none).
    std::size_t out_of_range_evaluations=0;
    double out_of_range_minimum_temperature_k=0,out_of_range_maximum_temperature_k=0;
    double out_of_range_minimum_pressure_pa=0,out_of_range_maximum_pressure_pa=0;
    double exchanged_mass_kg=0,elapsed_ms=0,entropy_change_j_per_k=0,entropy_roundoff_j_per_k=0;
    std::vector<double> species_mass_residual,species_mass_bound;
    MacEnergy energy_residual,energy_bound;
    // Adds one out-of-range evaluation at the given face state.
    void record_out_of_range(double temperature_k,double pressure_pa);
    // Accumulates counts and extremes of another report's out-of-range evaluations.
    void merge_out_of_range(const MacDiffusionReport&);
};
struct MacDiffusionAdvance { MacTransportState state; MacDiffusionReport report; };
// Occupied, gas-only finite volumes. No liquid/disease/passive diffusion.
// Exact empty cells are admitted; faces touching one carry no diffusive flux.
// Heun predictor/corrector; coefficients follow each stage's actual T,p,X.
// Face fluxes are driven by the Maxwell-Stefan force including pressure
// diffusion, in a symmetric positive-semidefinite (entropy-producing) discrete
// form with zero net mass; they carry species enthalpy. Invalid candidates
// retry before publication. No clipping or trace cutoffs.
MacDiffusionAdvance advance_mac_diffusion(const SpeciesRegistry&,const FluidThermodynamics&,
    const MacTransportState&,double seconds,const MacDiffusionSettings&,
    const GasTransportProfile&,std::size_t attempt_budget=4096,
    MacDiffusionRounding=MacDiffusionRounding::reference);
// Sufficient zero-flux certificate: every open face joins identical inventories
// (masses and U), or touches an empty cell.
bool mac_diffusion_stationary(const SpeciesRegistry&,const MacTransportState&);
}
