#pragma once
// Whole-asteroid import (docs/colony-owner.md#import). Turns the game's
// one-element-per-cell inventory into a near-equilibrium owner state without
// creating or deleting mass.
#include "oni_catalogue.hpp"
#include "mixed_atmospheres/mac_transport.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace ma::oni {
enum class ImportCellKind : std::uint8_t { solid = 0, fluid = 1, space = 2 };
inline constexpr std::uint32_t import_vacuum = 0xffffffffU;
struct ImportCell {
    ImportCellKind kind = ImportCellKind::solid;
    std::uint32_t species = import_vacuum; // catalogue species, or import_vacuum
    double mass_kg = 0, temperature_k = 0;
};
// Liquid that cannot fit in its sealed region at its zero-pressure density.
// It stays booked to that region and is never deleted.
struct ReservoirEntry { std::size_t region_cell = 0; std::size_t species = 0; double mass_kg = 0, temperature_k = 0; };
struct ImportReport {
    std::size_t regions = 0, liquid_bodies = 0, raised_cells = 0, headspaces_filled = 0, vacuum_cells_filled = 0;
    double reservoir_kg = 0, moved_gas_mol = 0, condensed_kg = 0, evaporated_kg = 0;
    double maximum_relative_pressure_spread = 0; // after settling, over regions with gas
    double maximum_relative_mass_error = 0;      // per species, imported against settled plus reservoir
};
struct ImportResult { experimental::MacTransportState state; std::vector<ReservoirEntry> reservoir; ImportReport report; };
// width * height cells, row-major from the bottom row. Space cells stay empty
// (they are the open-sky sink) and solid cells become walls. Liquid is levelled
// within each connected body: cells covered by liquid or rock hold their
// hydrostatic mass, the body's remaining liquid fills its surface cells to a
// common level, and liquid beyond the surface rises into the gas above or, in
// a sealed region, goes to the reservoir. Vapour beyond its saturation
// pressure first condenses into its liquid at the bottom of its region, and a
// liquid under less gas pressure than its own vapour pressure (a pool beside
// vacuum) gives its region saturated vapour. Gas in
// each connected region is then laid out in density layers at one pressure,
// filling the headspace of partly filled liquid cells and vacuum pockets.
[[nodiscard]] ImportResult settle_import(const Catalogue&, std::size_t width, std::size_t height,
    std::span<const ImportCell> cells, double gravity_m_per_s2);
}
