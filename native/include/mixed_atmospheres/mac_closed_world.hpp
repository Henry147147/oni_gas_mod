#pragma once
#include "mac_thermal.hpp"
#include "mac_diffusion.hpp"
#include "mac_viscosity.hpp"
#include "mac_shock_heat.hpp"
#include "mac_reconstruction.hpp"
#include "mac_compression.hpp"
#include "mac_compressive_work.hpp"
#include "volume_work.hpp"
#include "mac_phase_change.hpp"
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace ma::experimental {
// Persisted selectors. The original index-space linear path keeps value 1.
// Value 3 reconstructs a piecewise-linear gas/liquid interface (PLIC) in cells
// holding both phases, so swept parcels carry each phase only where it is.
enum class MacMaterialReconstruction { donor_cell = 0, limited_linear = 1, swept_first_moment = 2, interface_plic = 3 };
// Value 4 is the split pressure-sensed acoustic law with the monotonic
// (Christensen/LULESH) limiter: linear term * (1 - phi), quadratic * (1 - phi^2).
// Reference path only; the production kernel falls back to the reference.
enum class MacCompressionViscosity { none = 0, quadratic = 1, acoustic_quadratic = 2, coupled_pressure = 3, monotone_acoustic_quadratic = 4 };
// Persisted values: 0 disabled, 1 closing-speed, 2 matched Cp/max-speed,
// 3 matched Cp with two half-cell resistances, 4 value 2 matched to the
// monotone compression law (reference path only). Older values keep their laws.
enum class MacShockHeatRegularization {
    none = 0, compression_activated = 1, pressure_sensed_acoustic = 2, pressure_sensed_series = 3, monotone_pressure_sensed = 4
};
// Persisted numerical implementation. Values never change meaning. Production
// keeps selected physical laws through audited reference fallback for unported
// selectors/range flags; value 1 is the narrower legacy interval port.
// Value 1 is reserved for the retired interval port and always rejects.
enum class MacNumericalKernel { reference = 0, retired_interval_audited = 1, production = 2, game = 3 };
// Persisted temporal pressure scheme. The moment path preserves pressure work
// with two Legendre moments; the surrounding split remains second order.
enum class MacPressureIntegration { discrete_gradient_midpoint = 0, two_moment = 1, two_moment_coupled = 2 };
// Persisted vacuum representation. Value 0: material entering an empty cell
// fills its grid volume. Value 1 (void phase, MAC_VACUUM.md): each cell keeps
// the volume its material occupies; the remainder is exact vacuum, and swept
// strips carry material and void by a PLIC line. Reference path, gas only.
enum class MacVacuumRepresentation { empty_cells = 0, void_phase = 1 };
// Explicit coarse-graining of unresolved gas-containing material. Value 0
// retains exact void geometry; value 1 expands material occupying at most
// 16 epsilons of grid volume into that cell, retaining all conserved fields.
enum class MacSubcellVoidPolicy { exact_geometry = 0, expand_unresolved_gas = 1 };
// Persisted phase change. Value 1: after each substep, every calibrated
// liquid/vapour pair relaxes to phase equilibrium at its pressure-dependent
// saturation point (MAC_PHASE_CHANGE.md). Reference path only.
enum class MacPhaseChange { none = 0, saturation = 1 };
// Persisted production step control. Value 0 tries twice the last accepted
// step and halves on every rejection. Value 1 (predictive) also limits the
// next and retried steps by the measured material-volume CFL ratio, so a
// flowing region does not spend most attempts on CFL rejections. Value 2
// advances closed fluid components independently, with one shared attempt
// budget and a final full-grid audit. Nonperiodic production void phase only.
enum class MacStepControl { doubling = 0, predictive = 1, regional_predictive = 2 };
// Saved game-profile family. Legacy retains every pre-profile trajectory;
// scientific gates never select the base-game accuracy-floor profile.
// Family 4 adds the free-surface closure to family 3: no face carries
// material out of an empty cell. Family 5 also sinks sub-threshold traces
// before conduction (native/MAC_GAME_KERNEL.md).
enum class MacGameProfile { legacy = 0, base_game_floor = 1, game_kernel = 2,
    game_kernel_positive_liquid = 3, game_kernel_free_surface = 4,
    game_kernel_trace_heat = 5 };
enum class MacGameLiquidLaw { linear = 0, positive_segments = 1 };
// Saved settings of the opt-in diagnostic game kernel; never selected by gates.
struct MacGameSettings {
    MacGameLiquidLaw liquid_law = MacGameLiquidLaw::linear;
    unsigned pressure_maximum_iterations = 200, heat_maximum_iterations = 200;
    double liquid_stiffness_ratio = .04;
    unsigned flow_parts = 2;
    double pressure_relative_tolerance = 1e-8;
    double gas_heat_mixing = 0;
    bool sharpen_liquid_interfaces = true;
    double interface_detail_fraction = 0;
    double phase_relaxation_time_s = 1;
    std::vector<MacSaturationPair> saturation_pairs;
    void validate() const;
};
struct MacClosedSettings {
    // Physical setting additions require updating the checkpoint schema and
    // its complete-identity/continuation controls in mac_checkpoint.cpp.
    double maximum_timestep_s = .01;
    double maximum_outgoing_volume_fraction = .4;
    PressurePreconditioner preconditioner = PressurePreconditioner::automatic;
    double gravity_m_per_s2 = 0; // downward, constant; vertical periodicity is unsupported
    MacMaterialReconstruction reconstruction = MacMaterialReconstruction::limited_linear;
    // Modes 1/2 use split compression half steps. Explicit mode 3 couples
    // compressive stress and its heat to the material pressure/volume root.
    MacCompressionViscosity compression_viscosity = MacCompressionViscosity::acoustic_quadratic;
    MacMomentumReconstruction momentum_reconstruction = MacMomentumReconstruction::limited_linear;
    bool thermal_conduction = false;
    MacShockHeatRegularization shock_heat = MacShockHeatRegularization::pressure_sensed_acoustic;
    MacViscosity viscosity;
    MacNumericalKernel numerical_kernel = MacNumericalKernel::reference;
    // Legacy/reference callers retain their trajectories. New game sessions
    // explicitly select physical diffusion; old checkpoints restore disabled.
    // Diffusion composes with either numerical kernel (it wraps each flow interval).
    MacDiffusionSettings diffusion=MacDiffusionSettings::disabled();
    MacPressureIntegration pressure_integration = MacPressureIntegration::discrete_gradient_midpoint;
    MacVacuumRepresentation vacuum = MacVacuumRepresentation::empty_cells;
    MacPhaseChange phase_change = MacPhaseChange::none;
    MacSubcellVoidPolicy subcell_void = MacSubcellVoidPolicy::exact_geometry;
    MacStepControl step_control = MacStepControl::doubling;
    // Opt-in free expansion below this occupied/grid volume fraction.
    // The legacy threshold retains its original substep schedule and bytes.
    double gas_expansion_volume_fraction = 16 * std::numeric_limits<double>::epsilon();
    MacGameProfile game_profile = MacGameProfile::legacy;
    // Per constituent per cell, strictly below the threshold. Zero disables
    // the booked trace sink; the user-authorized ceiling is one milligram.
    double trace_sink_threshold_kg = 0;
    // Saved game-profile policies; older images retain both original rules.
    bool compatible_void_pressure_work = false;
    bool recover_mixed_eos_endpoint = false;
    // Book newly formed traces after each production transport stage. Older
    // profiles retain their interval-boundary sink and exact continuation.
    bool trace_sink_per_substep = false;
    // Binary64 production EOS inside phase root searches; accepted endpoints
    // still use the strict reference EOS. Older profiles keep strict trials.
    bool fast_phase_trial_eos = false;
    MacGameSettings game;
    void validate() const;
};
struct MacClosedSubstep {
    MacTransportState state;
    MacTransportState pressure_state;
    OccupiedMaterialPressureStep pressure;
    std::optional<MacCompressionCoefficients> compressive_coefficients;
    std::vector<MacCompressiveWork> compressive_responses;
    std::vector<MacEnergy> compressive_linear_speeds;
    std::vector<double> root_input_velocities{}, root_output_velocities{};
    std::vector<MacScaled> full_pressure_velocities{};
    MacMassFluxes volume_fluxes_m3, mass_fluxes_kg;
    MacTransportReport transport;
    std::optional<MacParcelPlan> reconstruction;
    // Occupied pressure cells are compacted in ascending grid-cell order;
    // exact vacuum has no pressure unknown. The two face
    // arrays contain the corresponding compact pressure-node index, or max().
    std::vector<std::size_t> active_cells, pressure_nodes_x, pressure_nodes_y;
    std::vector<double> material_volumes_m3, geometric_residuals_m3;
    // Void phase: each cell's swept volume (grid volume plus face sweeps),
    // holding its material volume and its unchanged void. Otherwise empty.
    std::vector<double> swept_volumes_m3;
    double pressure_energy_residual_j = 0, pressure_energy_bound_j = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    MacEnergy pressure_energy_residual_wide_j, pressure_energy_bound_wide_j;
    MacEnergy energy_residual_wide_j, energy_bound_wide_j;
    std::size_t rounded_energy_projections = 0;
    double pressure_wall_impulse_x = 0, pressure_wall_impulse_y = 0;
    MacScaled full_pressure_wall_impulse_x{}, full_pressure_wall_impulse_y{};
    double body_impulse_y = 0;
    MacScaled full_body_impulse_y{};
    std::vector<double> gravity_body_work_y, gravity_potential_flux_y, gravity_internal_work_y;
    // Gravity doubles are diagnostic projections. Accounts use these full
    // operands, including work and mass flux below binary64 range.
    std::vector<MacEnergy> full_gravity_body_work_y, full_gravity_potential_flux_y, full_gravity_internal_work_y;
    std::vector<double> pre_gravity_internal_energy_j, gravity_internal_changes_j;
    std::vector<MacStoredRemainder> pre_gravity_energy_remainders_j;
    std::vector<MacEnergy> full_gravity_internal_changes_j;
    double gravity_internal_residual_j = 0, gravity_internal_bound_j = 0;
    double potential_transport_residual_j = 0, potential_transport_bound_j = 0;
    MacEnergy gravity_internal_residual_wide_j, gravity_internal_bound_wide_j;
    MacEnergy potential_transport_residual_wide_j, potential_transport_bound_wide_j;
    MacEnergy gravity_internal_change_wide_j;
    std::size_t rounded_gravity_projections = 0;
    double gravity_internal_change_j = 0, entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    // Concavity upper bound on the gravity internal change's entropy (sum of
    // dU / T at the pre-gravity state) and its operand sum |dU| / T.
    double gravity_entropy_bound_j_per_k = 0, gravity_entropy_operands_j_per_k = 0;
    std::size_t unresolved_momentum_updates = 0;
    std::size_t rounded_pressure_velocity_inputs = 0;
    // Actual full midpoint sweeps, distinct from the binary64 pressure-root
    // volume parameter. Coupled work must close locally against these values.
    std::vector<MacScaled> full_compressive_volume_changes_m3;
    std::vector<MacEnergy> full_compressive_internal_work_j;
    MacEnergy full_compressive_work_residual_j, full_compressive_work_bound_j;
    std::size_t unresolved_compressive_work_updates = 0;
    std::size_t unresolved_gravity_internal_updates = 0;
    MacPrimaryRoundingCounts primary_energy{};
};
// Closed regular-grid Lagrange/projection split. Exact empty cells contribute
// geometry and zero traction, without a pressure unknown. Work pressure acts
// directly on canonical face J; the same midpoint sweeps define material
// volume and reconstructed material parcels for compatible transport. Gravity work
// is reconciled with actual potential-energy mass flux, with a signed internal
// energy change kept separate from transport mixing/projection heat. No
// diffusion, free surface, phase transition or game dispatch is supplied by this
// flow substep. MacClosedWorld composes diffusion around its flow intervals.
[[nodiscard]] MacClosedSubstep advance_mac_closed_substep(const SpeciesRegistry&, const FluidThermodynamics&,
    const MacTransportState&, double seconds, const MacClosedSettings& = {});

// External material removed by the saved game profile, never an unbooked
// numerical loss. Energies and canonical momenta are actual stored changes.
struct MacTraceSinkReport {
    std::vector<MacScaled> mass_kg;
    MacEnergy internal_energy_j, kinetic_energy_j, potential_energy_j;
    MacScaled momentum_x, momentum_y;
    PassiveQuantities passives;
    std::size_t constituents = 0, cells = 0;
    MacPrimaryRoundingCounts primary_energy;
    void merge(const MacTraceSinkReport&);
};
struct MacClosedAdvanceReport {
    MacTraceSinkReport trace_sink;
    std::vector<double> game_converted_mass_kg;
    MacDiffusionReport diffusion;
    std::size_t interval_kernel_intervals = 0, interval_reference_fallbacks = 0;
    std::size_t interval_audit_exact_fallbacks = 0, interval_audit_retries = 0;
    std::size_t interval_rounded_updates = 0, interval_unresolved_updates = 0;
    std::size_t interval_multigrid_solves = 0, interval_multigrid_cycles = 0, interval_multigrid_fallbacks = 0;
    std::size_t interval_multigrid_levels = 0, interval_topology_builds = 0;
    std::size_t substeps = 0, attempts = 0, rejected_attempts = 0;
    std::size_t exact_equilibrium_intervals = 0;
    std::size_t pressure_iterations = 0, nonlinear_iterations = 0;
    std::size_t pressure_increment_restarts = 0;
    std::size_t unresolved_heat_updates = 0, unresolved_momentum_updates = 0;
    std::size_t unresolved_gravity_internal_updates = 0;
    std::size_t compression_events = 0, unresolved_compression_heat_updates = 0, unresolved_compression_momentum_updates = 0;
    std::size_t unresolved_compressive_work_updates = 0;
    std::size_t momentum_correction_events = 0, limited_momentum_corrections = 0;
    std::size_t unresolved_momentum_corrections = 0, unresolved_remainder_corrections = 0;
    std::size_t conduction_stages = 0, conduction_contacts = 0, conduction_iterations = 0, unresolved_conduction_updates = 0;
    MacEnergy conductive_redistribution_j;
    double conductive_entropy_j_per_k = 0;
    std::size_t viscosity_stages = 0, viscosity_modes = 0;
    std::size_t unresolved_viscous_heat_updates = 0, unresolved_viscous_momentum_updates = 0;
    MacEnergy viscous_heat,viscous_stored_heat;
    MacScaled viscous_wall_impulse_x,viscous_wall_impulse_y;
    double viscous_entropy_lower_bound_j_per_k = 0;
    double energy_residual_j = 0, energy_bound_j = 0;
    double mixing_heat_j = 0, wall_heat_j = 0;
    double wall_impulse_x = 0, wall_impulse_y = 0;
    MacScaled full_wall_impulse_x{}, full_wall_impulse_y{};
    double maximum_geometric_residual_m3 = 0;
    double gravity_internal_change_j = 0, body_impulse_y = 0;
    MacEnergy gravity_internal_change_wide_j;
    MacScaled full_body_impulse_y{};
    std::size_t rounded_gravity_projections = 0;
    double entropy_change_j_per_k = 0, entropy_roundoff_j_per_k = 0;
    // Accepted gravity reconciliations' entropy bound. Interval audits require
    // entropy_change - gravity_entropy_bound >= -entropy_roundoff.
    double gravity_entropy_bound_j_per_k = 0;
    MacEnergy energy_residual_wide_j, energy_bound_wide_j, mixing_heat_wide_j, wall_heat_wide_j;
    MacEnergy remap_work_wide_j;
    std::size_t physical_admission_backtracks = 0;
    MacEnergy compression_heat_wide_j;
    double compression_heat_j = 0;
    std::size_t rounded_energy_projections = 0;
    std::size_t shock_heat_stages = 0, shock_heat_events = 0, shock_heat_root_iterations = 0;
    std::size_t rejected_shock_heat_trials = 0, unresolved_shock_heat_updates = 0;
    MacEnergy absolute_shock_heat_transfer_wide_j;
    double absolute_shock_heat_transfer_j = 0;
    MacPrimaryRoundingCounts primary_energy{};
    std::size_t phase_change_stages = 0, evaporating_cells = 0, condensing_cells = 0;
    double evaporated_kg = 0, condensed_kg = 0, phase_change_entropy_j_per_k = 0;
    std::size_t subcell_void_events = 0;
    double subcell_void_entropy_j_per_k = 0;
};
struct MacClosedAdvance;
// Reference/exact_equilibrium retain the saved numerical selection. An
// explicit production request selects and persists numerical kernel 2, except
// for an already selected game kernel (3). Kernel 2 uses
// exact_equilibrium's certificate and the production substep kernel under the
// interval contract. Restored production owners need no execution-policy flag.
enum class MacExecutionPolicy { reference, exact_equilibrium, production };
struct MacFluidTransferResult;
struct MacTraceSink;
struct MacPhaseRelaxation;
struct MacLiquidSettling;
struct MacLiquidSettlingReport { std::size_t faces = 0; double moved_kg = 0, released_j = 0; };
// Passives (germs) move with mass: an injection adds its own, an extraction
// takes the cell's passives in proportion to the mass it removes (all of them
// when it empties the cell) and returns them on its parcel.
struct MacFluidTransfer {
    std::size_t cell_index, species_index;
    bool inject;
    double mass_kg, temperature_k;
    PassiveQuantities passives{};
    // Optional adapter accounting metadata; never changes physical arithmetic.
    bool observe_passives = false;
};
struct MacFluidParcel {
    std::size_t cell_index, species_index;
    bool injected;
    double mass_kg, temperature_k, internal_energy_j;
    MacEnergy resolved_kinetic_energy, potential_energy;
    MacScaled momentum_x, momentum_y;
    PassiveQuantities passives{};
    PassiveQuantities passives_before{}, passives_after{};
};
using MacGasTransfer = MacFluidTransfer;
using MacGasParcel = MacFluidParcel;
using MacGasTransferResult = MacFluidTransferResult;
enum class MacFluidTransferTopology { single_phase, occupied_mixture };
// A moving material parcel. All mechanics use the core-local coordinates and
// carry their full scaled range; temperature is derived from the final U.
struct MacMovingParcel {
    std::size_t cell_index = 0, species_index = 0;
    double mass_kg = 0;
    MacEnergy internal_energy_j, kinetic_energy_j, potential_energy_j;
    MacScaled momentum_x, momentum_y;
    PassiveQuantities passives{};
};
struct MacParcelPassiveCredit { std::uint64_t identity = 0; MacEnergySum quantity; };
struct MacParcelGridCredit {
    // Exact additive changes of the represented native operands. Iterate all
    // components; value() is only a rounded estimate, not settlement authority.
    MacEnergySum mass_kg, internal_energy_j, kinetic_energy_j, potential_energy_j;
    MacEnergySum momentum_x, momentum_y;
    std::vector<MacParcelPassiveCredit> passives;
};
struct MacParcelDepositReceipt {
    // Nominal admitted source portion before primary-write rounding. Exact
    // grid credits plus work/impulse define settlement; keep every remainder.
    MacMovingParcel consumed;
    MacParcelGridCredit grid;
    MacEnergySum kinetic_mixing_heat_j, potential_work_j;
    // Impulse on the fluid. Grid DeltaJ = consumed J + wall impulse.
    MacScaled wall_impulse_x, wall_impulse_y;
    PassiveQuantities passives_before{}, passives_after{};
};
enum class MacParcelRefusal { none, invalid_request, unrepresentable_primary,
    invalid_equilibrium, conservation };
struct MacParcelDepositResult;
struct MacCellHeat {
    std::size_t cell_index;
    double joules, maximum_temperature_k;
};
// A change to one passive quantity of a cell (germs growing or dying).
struct MacPassiveEdit {
    std::size_t cell_index;
    std::uint64_t identity;
    double change;
};
// Owns its registry, immutable EOS identity, canonical state and clock. Advance
// creates a new owner; failed later attempts publish neither time nor state.
class MacClosedWorld {
public:
    MacClosedWorld(SpeciesRegistry, std::shared_ptr<const FluidThermodynamics>, MacTransportState, MacClosedSettings = {},
        std::shared_ptr<const GasTransportProfile> = GasTransportProfile::standard());
    // New-colony import from declared temperatures, never a save migration.
    [[nodiscard]] static MacClosedWorld game_colony(SpeciesRegistry,
        std::shared_ptr<const FluidThermodynamics>, MacTransportState,
        std::span<const double> temperatures, MacClosedSettings);
    [[nodiscard]] ReferenceEquilibriumState cell_reading(std::size_t cell) const;
    [[nodiscard]] const MacTransportState& state() const noexcept { return *state_snapshot_; }
    [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] const FluidThermodynamics& thermodynamics() const noexcept { return *thermodynamics_; }
    [[nodiscard]] const MacClosedSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const GasTransportProfile& transport_profile() const noexcept { return *transport_; }
    [[nodiscard]] MacClosedWorld with_diffusion(MacDiffusionSettings) const;
    [[nodiscard]] double time_s() const noexcept { return time_s_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] MacClosedAdvance advanced(double seconds, std::size_t attempt_budget = 1024,
        MacExecutionPolicy = MacExecutionPolicy::reference) const;
    // Remove only constituents below the saved threshold, returning a named
    // sink receipt for mass, U+K+G, canonical J and carried passives. Legacy
    // and unchanged inventories return the original shared owner/revision.
    [[nodiscard]] MacTraceSink with_trace_sink() const;
    // Immutable external heat exchange. Retains time, geometry, every mass and
    // canonical face momentum; checks the complete batch before returning it.
    [[nodiscard]] MacClosedWorld with_cell_heat(std::span<const MacCellHeat>,MacPrimaryRoundingCounts* accepted_rounding=nullptr) const;
    // One physical mixture is one ordered atomic batch. A normal refusal
    // returns the original world and no consumed receipts; no fallback runs.
    [[nodiscard]] MacParcelDepositResult with_moving_parcels(std::span<const MacMovingParcel>) const;
    // Declared point sources, not an aperture/pump flow model. Injection is at
    // rest; extraction carries the selected species at the local face speeds.
    // Filled gas cells without passive metadata only; unsupported void/liquid
    // topology rejects atomically. Packets account for all carried mechanics.
    [[nodiscard]] MacGasTransferResult with_gas_transfers(std::span<const MacGasTransfer>) const;
    // Also admits a filled, single-liquid cell of the requested material.
    // The declared parcel is at the receiving cell's pre-edit pressure;
    // extraction carries local specific internal energy, including elastic U.
    // This is a point source contract, with no implicit pump/enthalpy work.
    // Gas/liquid mixing, void creation and passive metadata reject atomically.
    [[nodiscard]] MacFluidTransferResult with_fluid_transfers(std::span<const MacFluidTransfer>) const;
    // Explicit opt-in for gas/liquid coexistence. The same parcel/accounting
    // contract applies, and the complete result must remain occupied. This
    // does not supply free-surface dynamics or a device work reservation.
    [[nodiscard]] MacFluidTransferResult with_fluid_transfers(std::span<const MacFluidTransfer>, MacFluidTransferTopology) const;
    // Terrain edits (docs/colony-owner.md#terrain). Opening turns walls into
    // exact vacuum cells (void phase or empty-cell inventory). Closing turns
    // empty cells into walls: the normal momentum on their faces is removed and
    // its kinetic energy becomes internal energy of the neighbour that carried
    // it, so U+K is unchanged and mass is untouched.
    // Frame-level phase change for an owner whose kernel has none (the
    // production kernel; docs/colony-owner.md#phase-change): every cell holding
    // a saturation pair relaxes at fixed U and volume (advance_mac_phase_change).
    // Momentum and the clock are unchanged; species masses move within pairs.
    // fraction: the share of the way to equilibrium each cell moves.
    [[nodiscard]] MacPhaseRelaxation with_phase_relaxation(std::span<const MacSaturationPair>, double fraction = 1) const;
    // The same fixed-U equilibrium, restricted to explicit cell indices.
    [[nodiscard]] MacPhaseRelaxation with_phase_relaxation_in_cells(std::span<const MacSaturationPair>,
        std::span<const std::size_t> cells, double fraction = 1) const;
    // Buoyant settling of immiscible liquids (MAC_TRANSPORT.md, "Liquid
    // settling"): across each vertical face of two full liquid-bearing cells,
    // the lower cell's lightest liquid phase rises and the upper cell's
    // heaviest sinks when it is denser, as equal zero-pressure volumes:
    // fraction (at most 1/2) of the smaller. Each parcel carries its donor's
    // specific internal energy and passives; the potential energy released is
    // heat, momentum is unchanged and its kinetic change is booked, so U+K+G
    // and every constituent are conserved.
    [[nodiscard]] MacLiquidSettling with_liquid_settling(double fraction) const;
    // Faces closed for the next steps (MacGrid::closed_x/closed_y; empty
    // masks open every face). A face that closes while carrying momentum
    // loses it, and its kinetic energy becomes internal energy of its two
    // cells by mass share, so U+K is unchanged and mass is untouched.
    [[nodiscard]] MacClosedWorld with_closed_faces(std::vector<std::uint8_t> x, std::vector<std::uint8_t> y) const;
    // Adds each change to its cell's passive (a cell holding material only);
    // a quantity that reaches zero or below is removed. Mass, U and momentum
    // are unchanged.
    [[nodiscard]] MacClosedWorld with_passive_edits(std::span<const MacPassiveEdit>) const;
    [[nodiscard]] MacClosedWorld with_opened_cells(std::span<const std::size_t>) const;
    [[nodiscard]] MacClosedWorld with_closed_cells(std::span<const std::size_t>) const;
    // Physical owner checkpoint, not a scalar/display projection. Restoring
    // requires the exact registry/EOS identity and validates the complete image.
    [[nodiscard]] std::vector<std::uint8_t> checkpoint() const;
    [[nodiscard]] static MacClosedWorld from_checkpoint(SpeciesRegistry,
        std::shared_ptr<const FluidThermodynamics>,std::span<const std::uint8_t>,
        std::shared_ptr<const GasTransportProfile> = GasTransportProfile::standard());
private:
    [[nodiscard]] MacClosedAdvance advanced_without_trace_sink(double,std::size_t,MacExecutionPolicy) const;
    [[nodiscard]] MacClosedAdvance advanced_flow(double,std::size_t,MacExecutionPolicy,bool validation_reference = false) const;
    // The next revision of this owner holding a state its operation already
    // checked: same registry, EOS identity (shared, not copied), settings and
    // clock, with void-phase occupied volumes canonical.
    [[nodiscard]] MacClosedWorld derived(MacTransportState next) const;
    struct Derived {};
    MacClosedWorld(Derived, const MacClosedWorld& from, MacTransportState state);
    friend struct MacGameKernelAccess;
    friend struct MacKernelAccess; // production kernel publication (mac_kernel.cpp)
    SpeciesRegistry registry_;
    std::shared_ptr<const FluidThermodynamics> thermodynamics_;
    void replace_state(MacTransportState);
    std::shared_ptr<const MacTransportState> state_snapshot_;
    MacClosedSettings settings_;
    std::shared_ptr<const GasTransportProfile> transport_;
    double time_s_ = 0;
    std::uint64_t revision_ = 1;
};
struct MacClosedAdvance { MacClosedWorld world; MacClosedAdvanceReport report; };
struct MacTraceSink { MacClosedWorld world; MacTraceSinkReport report; };
struct MacPhaseRelaxation { MacClosedWorld world; MacPhaseChangeReport report; };
struct MacLiquidSettling { MacClosedWorld world; MacLiquidSettlingReport report; };
struct MacParcelDepositResult {
    MacClosedWorld world;
    std::vector<MacParcelDepositReceipt> receipts;
    MacParcelRefusal refusal = MacParcelRefusal::none;
    std::string reason;
    MacEnergy energy_residual_j, energy_bound_j;
    MacScaled momentum_x_residual, momentum_x_bound;
    MacScaled momentum_y_residual, momentum_y_bound;
    MacPrimaryRoundingCounts primary_energy{};
    [[nodiscard]] bool accepted() const noexcept { return refusal == MacParcelRefusal::none; }
};
struct MacFluidTransferResult {
    MacClosedWorld world;
    std::vector<MacFluidParcel> parcels;
    std::size_t unresolved_heat_updates = 0;
    MacPrimaryRoundingCounts primary_energy{};
};
}
