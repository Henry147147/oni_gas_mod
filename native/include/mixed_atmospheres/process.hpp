#pragma once
#include "mixture.hpp"

namespace ma {
// A finite amount of material carried through a steady-flow device. Its
// inventory holds U; the stream carries H = U + pV. Negligible bulk kinetic
// energy and elevation changes are assumed by these reference unit operations.
struct FlowPacket {
    MixtureInventory contents;
    double pressure_pa = 101325;
    void validate(const SpeciesRegistry& registry) const;
    [[nodiscard]] double volume(const SpeciesRegistry& registry) const;
    [[nodiscard]] double enthalpy(const SpeciesRegistry& registry) const;
};

struct PumpResult {
    FlowPacket output;
    double work_in_j = 0;
    double entropy_produced_j_per_k = 0;
};

// Constant-property ideal-gas compressor / incompressible-liquid pump.
// Positive pressure rise requires positive shaft work. Inefficiency heats the
// output instead of deleting energy. These functions quote a process without
// mutating its input or consuming a power budget.
[[nodiscard]] PumpResult compress_gas(const SpeciesRegistry& registry, const FlowPacket& feed,
                                      double outlet_pressure_pa, double isentropic_efficiency);
[[nodiscard]] PumpResult pump_liquid(const SpeciesRegistry& registry, const FlowPacket& feed,
                                    double outlet_pressure_pa, double hydraulic_efficiency);

struct SeparationResult {
    FlowPacket selected;
    FlowPacket remainder;
    double reversible_work_j = 0;
    double work_in_j = 0;
    double heat_rejected_j = 0;
    double heat_rejection_temperature_k = 0;
    double entropy_produced_j_per_k = 0;
};

// Isothermal/isobaric ideal-gas separation of a selected constituent, with a
// specified recovery fraction. Accounts for the minimum demixing work, actual
// work, and heat rejected to a reservoir at the feed temperature. An installed
// device must reserve this power AND provide the heat sink before committing.
[[nodiscard]] SeparationResult separate_gas(const SpeciesRegistry& registry, const FlowPacket& feed,
                                            std::size_t species, double recovery_fraction,
                                            double reversible_efficiency);
}
