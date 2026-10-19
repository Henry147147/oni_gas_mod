#pragma once
#include "mixed_atmospheres/mac_energy.hpp"
#include <cstdint>
#include <optional>

namespace ma::experimental::mac_kernel {
// Derived dyadic time coordinates. No owner clock or authoritative state is
// kept here. Adjacent cells partition a face on the finer of their two grids.
struct TimeBasisPiece {
    std::uint32_t interval = 0;
    double midpoint_p1 = 0, relative_duration = 1;
};
struct TimeMortarPiece {
    unsigned level = 0;
    std::uint32_t piece = 0, count = 1;
    TimeBasisPiece left, right;
};
[[nodiscard]] TimeMortarPiece time_mortar_piece(unsigned left_level, unsigned right_level, std::uint32_t piece);

struct FullTimeFaceFlux { MacEnergy momentum, volume, first_volume; };
struct TimeFaceFlux {
    double momentum = 0, volume = 0, first_volume = 0;
    // Allocated significands/exponents are used only after a flagged ordinary
    // calculation. Physical storage rounding belongs to the caller's audit.
    std::optional<FullTimeFaceFlux> full;
};
struct TimeMoment { double value = 0; std::optional<MacEnergy> full; };
// Force on this piece is mean_force + P1_piece * first_force. One quadratic
// momentum path supplies BOTH shared volume moments. Inputs are representable;
// flagged derived products retain their range rather than clipping a flux.
[[nodiscard]] TimeFaceFlux integrate_time_face(double momentum, double mean_force, double first_force,
    double inertia, double area, double macro_seconds, unsigned level);
// Re-express the SAME face moments in either cell's local time basis.
[[nodiscard]] TimeMoment time_cell_first_moment(const TimeBasisPiece&, double volume, double first_volume);
[[nodiscard]] MacEnergy time_cell_first_moment(const TimeBasisPiece&, MacEnergy volume, MacEnergy first_volume);
}
