#!/usr/bin/env python3
"""Metrics of one fixed comparison arena (docs/base-game-floor.md, F1-F5).

Reads floor-<id>.json and floor-<id>.txt as FloorArenas.cs writes them in a
harness run (ONI's own simulation or the colony owner) and prints the
protocol's metrics as JSON. Events persist for the next five samples (1 s);
their time is the first sample of that run, reported as a bracket from the
sample before. Final metrics average the last 1 s (five samples).

    python3 tools/floor_metrics.py RUN_DIR/.../colony-validation/floor-f1.json
"""
import argparse
import json
import math
import struct
from pathlib import Path

GAS_CONSTANT = 8.314462618
PERSIST = 5          # samples after the first that must also hold (1 s)
FINAL = 5            # samples averaged for final metrics (1 s)


class Arena:
    def __init__(self, record: dict, rows: list[list[float]]):
        self.record = record
        self.width, self.height = record["width"], record["height"]
        self.dt = record["tick_seconds"]
        self.elements = {int(k): v for k, v in record["elements"].items()}
        self.ticks = record["ticks"]
        self.rows = rows  # [tick, (element, mass, temperature) per interior cell]
        # Legacy float.ToString("R") samples used float-round-trip text. Put
        # their original binary32 value back before deriving heat/pressure.
        self.temperature_storage = record.get("temperature_storage", "binary32")
        if self.temperature_storage not in ("binary32", "binary64"):
            raise ValueError("Unknown floor temperature storage")

    def cells(self, row):
        """{(x, y): ([(element description, kg), ...], temperature)} of one sample."""
        out, k = {}, 1
        # Records before constituent lists hold one element per cell.
        listed = "constituent count" in self.record.get("cells", "")
        for y in range(1, self.height - 1):
            for x in range(1, self.width - 1):
                n = int(row[k]) if listed else 1; k += 1 if listed else 0
                parts = [(self.elements[int(row[k + 2 * j])], row[k + 2 * j + 1]) for j in range(n)]
                k += 2 * n
                temperature = row[k]
                if self.temperature_storage == "binary32":
                    temperature = struct.unpack("<f", struct.pack("<f", temperature))[0]
                out[(x, y)] = (parts, temperature); k += 1
        return out

    def series(self, start: int):
        """(time after `start`, cells) for every sample from that tick on."""
        return [((row[0] - start) * self.dt, self.cells(row)) for row in self.rows if row[0] >= start]

    def row_at(self, tick: int):
        return next(self.cells(row) for row in self.rows if row[0] >= tick)


def pressure(cells, region) -> float:
    """Volume-average ideal pressure of a region, empty cells included (1 m^3 each)."""
    total = 0.0
    for c in region:
        parts, temperature = cells[c]
        for element, m in parts:
            if element["phase"] == "gas" and m > 0:
                total += m * GAS_CONSTANT * temperature / element["molar_mass_kg_per_mol"]
    return total / len(region)


def mass(cells, region, phase=None, element_id=None) -> float:
    return sum(m for c in region for e, m in cells[c][0]
               if (phase is None or e["phase"] == phase) and (element_id is None or e["id"] == element_id))


def constituents(cells, region):
    """(element, kg, temperature) of every constituent in the region."""
    return [(e, m, cells[c][1]) for c in region for e, m in cells[c][0]]


def event(series, holds):
    """First sample that holds with the next five: {"bracket_s": [before, at]}, or censored."""
    ok = [holds(cells) for _, cells in series]
    for i in range(len(ok) - PERSIST):
        if all(ok[i:i + PERSIST + 1]):
            return {"bracket_s": [series[i - 1][0] if i > 0 else series[0][0], series[i][0]]}
    return {"censored": True, "observed_s": series[-1][0] if series else 0}


def final(series, value):
    tail = series[-FINAL:]
    return sum(value(cells) for _, cells in tail) / len(tail)


def rect(x0, x1, y0, y1):
    return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]


def gas_totals(cells, region):
    parts = constituents(cells, region)
    moles = sum(m / e["molar_mass_kg_per_mol"] for e, m, _ in parts if e["phase"] == "gas" and m > 0)
    heat = sum(m * e["specific_heat_j_per_kg_k"] * t for e, m, t in parts if e["phase"] != "vacuum" and m > 0)
    return moles, heat


def gas_rooms(arena: Arena, start: int, left, right, open_cells):
    series = arena.series(start)
    first = series[0][1]
    moles, _ = gas_totals(first, open_cells)
    temperature = sum(m * t for e, m, t in constituents(first, open_cells) if e["phase"] == "gas") / max(mass(first, open_cells, "gas"), 1e-300)
    target = moles * GAS_CONSTANT * temperature / len(open_cells)
    near = lambda cells: all(abs(pressure(cells, room) - target) <= .05 * target for room in (left, right))
    total = mass(first, open_cells, "gas")
    out = {"analytic_final_pressure_pa": target, "initial_gas_kg": total, "initial_heat_j": gas_totals(first, open_cells)[1],
           "equalised": event(series, near),
           "final_pressure_pa": {"left": final(series, lambda c: pressure(c, left)), "right": final(series, lambda c: pressure(c, right))},
           "final_mass_fraction": {"left": final(series, lambda c: mass(c, left, "gas")) / total,
                                   "right": final(series, lambda c: mass(c, right, "gas")) / total}}
    return out, series


def f1(arena: Arena):
    left, right, divider = rect(1, 8, 1, 8), rect(10, 17, 1, 8), rect(9, 9, 1, 8)
    out, series = gas_rooms(arena, arena.ticks["release"], left, right, left + right + divider)
    out["final_mass_fraction"]["divider"] = final(series, lambda c: mass(c, divider, "gas")) / out["initial_gas_kg"]
    out["truth_mass_fraction"] = {"left": 64 / 136, "right": 64 / 136, "divider": 8 / 136}
    return out


def f2(arena: Arena):
    left, right, door = rect(1, 8, 1, 8), rect(10, 17, 1, 8), [(9, 4), (9, 5)]
    t = arena.ticks
    held_from, held_to = arena.row_at(t["locked"]), arena.row_at(t["commanded"])
    initial = mass(held_from, left + right, "gas")
    leak = (mass(held_to, right, "gas") - mass(held_from, right, "gas")) / initial
    if t["opened"] < 0:
        return {"closed_hold_leak_fraction": leak, "opened": "censored"}
    open_cells = left + right + door
    out, series = gas_rooms(arena, t["release"], left, right, open_cells)
    energies = sample_energies(arena)
    if energies is not None:
        active = [(row, audit, energy) for row, audit, energy in
                  zip(arena.rows, arena.record["owner_sample_audits"], energies) if row[0] >= t["release"]]
        release_energy = active[0][2]
        closure = []
        for _, audit, energy in active:
            for key in ("reservoir_kg", "space_sink_kg", "injected_kg", "extracted_kg"):
                if audit[key] != active[0][1][key]:
                    raise ValueError("F2 post-release mass exchange needs a separate energy account")
            if abs(energy["fluid_mass_kg"] - release_energy["fluid_mass_kg"]) > 32 * math.fsum(
                    math.ulp(e["fluid_mass_kg"]) for e in (energy, release_energy)):
                raise ValueError("F2 final pressure truth requires a constant sealed fluid mass")
            terms = (energy["internal_energy_j"], energy["kinetic_energy_j"], energy["potential_energy_j"],
                     -release_energy["internal_energy_j"], -release_energy["kinetic_energy_j"],
                     -release_energy["potential_energy_j"], -energy["accepted_external_heat_j"],
                     release_energy["accepted_external_heat_j"], energy["trace_sink_energy_j"],
                     -release_energy["trace_sink_energy_j"])
            residual = abs(math.fsum(terms))
            # A constant isolated reservoir is outside the fluid energy budget.
            # Require closure up to the binary64 observations' roundoff, rather
            # than silently importing an unmeasured reservoir energy exchange.
            if residual > 32 * math.fsum(math.ulp(value) for value in terms):
                raise ValueError("F2 fluid energy does not close with accepted heat and trace accounts")
            closure.append(residual)
        if any(e["id"] != "Oxygen" and m > 0 for _, cells in series for e, m, _ in constituents(cells, open_cells)):
            raise ValueError("F2 energy truth requires the prescribed oxygen-only fields")
        tail = [energy for _, _, energy in active[-FINAL:]]
        average = lambda key: math.fsum(e[key] for e in tail) / len(tail)
        # One FINAL target, using all booked heat through the final window.
        # Thermal U was formed before projection, avoiding U/formation cancellation.
        available = math.fsum((release_energy["thermal_energy_j"], release_energy["kinetic_energy_j"],
                              release_energy["potential_energy_j"], average("accepted_external_heat_j"),
                              -release_energy["accepted_external_heat_j"], -average("trace_sink_energy_j"),
                              release_energy["trace_sink_energy_j"], release_energy["formation_energy_j"],
                              -average("formation_energy_j")))
        capacity, total_mass = average("heat_capacity_j_per_k"), average("fluid_mass_kg")
        if capacity <= 0 or total_mass <= 0:
            raise ValueError("F2 final caloric capacity/mass must be positive")
        # Every admissible equilibrium distribution lies between these exact
        # geometry bounds on G. No uniform-density/gravity approximation is hidden.
        potential = [total_mass * 9.81 * (y + .5) for _, y in open_cells]
        temperatures = [(available - max(potential)) / capacity, (available - min(potential)) / capacity]
        moles = final(series, lambda cells: gas_totals(cells, open_cells)[0])
        targets = [moles * GAS_CONSTANT * temperature / len(open_cells) for temperature in temperatures]
        if targets[0] <= 0 or not all(math.isfinite(p) for p in targets):
            raise ValueError("F2 final pressure truth has no positive finite range")
        out.pop("analytic_final_pressure_pa")
        out["analytic_final_pressure_range_pa"] = targets
        out["pressure_truth"] = {"source": "final energy ledger with equilibrium gravity bounds",
                                 "temperature_range_k": temperatures,
                                 "accepted_heat_after_release_j": average("accepted_external_heat_j") - release_energy["accepted_external_heat_j"],
                                 "release_kinetic_energy_j": release_energy["kinetic_energy_j"],
                                 "isolated_reservoir_kg": active[0][1]["reservoir_kg"],
                                 "maximum_fluid_energy_residual_j": max(closure)}
        out["equalised"] = pressure_event_range(series, left, right, targets)
    else:
        out["pressure_truth"] = {"source": "release-sample estimate; same-frame U/K/G unavailable", "unresolved": True}
    out.update(closed_hold_leak_fraction=leak, command_to_open_s=(t["opened"] - t["commanded"]) * arena.dt)
    return out


def pressure_event_range(series, left, right, targets):
    """Enclose event brackets for every fixed FINAL pressure in the range."""
    low, high = targets
    guaranteed = event(series, lambda cells: all(.95 * high <= pressure(cells, room) <= 1.05 * low
                                                for room in (left, right)))
    possible = {"censored": True, "observed_s": series[-1][0] if series else 0}
    for i in range(len(series) - PERSIST):
        allowed_low, allowed_high = low, high
        for _, cells in series[i:i + PERSIST + 1]:
            for room in (left, right):
                value = pressure(cells, room)
                allowed_low = max(allowed_low, value / 1.05)
                allowed_high = min(allowed_high, value / .95)
        if allowed_low <= allowed_high:
            possible = {"bracket_s": [series[i - 1][0] if i > 0 else series[0][0], series[i][0]]}
            break
    if guaranteed.get("censored"):
        return guaranteed if possible.get("censored") else {"unresolved": True, "possible": possible, "guaranteed": guaranteed}
    return {"bracket_s": [possible["bracket_s"][0], guaranteed["bracket_s"][1]]}


def depths(cells, columns, rows, element_id, full):
    return [mass(cells, [(x, y) for y in rows], element_id=element_id) / full for x in columns]


def f3(arena: Arena):
    full = arena.record["placed"]["Water_full_cell_kg"]
    columns, rows = range(1, 21), range(1, 13)
    series = arena.series(arena.ticks["release"])
    heights = lambda cells: depths(cells, columns, rows, "Water", full)
    level = event(series, lambda cells: max(heights(cells)) - min(heights(cells)) <= 1)
    mean_depth = final(series, lambda cells: sum(heights(cells)) / 20)
    first = series[0][1]
    return {"levelled": level, "final_mean_depth_m": mean_depth, "nominal_depth_m": 2.0, "depth_error_m": abs(mean_depth - 2.0),
            "initial_water_kg": mass(first, rect(1, 20, 1, 12), element_id="Water")}


def f4_placements(arena: Arena):
    """Frozen F4 fields, reconstructed from the binary32 placement contract.

    The first native export follows a conduction interval. These prescribed
    inputs supply caloric truth; their reconstruction does not by itself prove
    that a particular native run admitted them (the admission probe does that).
    """
    placed = arena.record.get("placed", {})
    required = ("hot_cell_kg", "cold_cell_kg", "granite_tile_kg")
    if not all(key in placed for key in required):
        return None
    descriptions = {element["id"]: element for element in arena.elements.values()}
    stored = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    cells = {}
    for y in (1, 2):
        for x in range(1, 6):
            key, name, temperature = (("hot_cell_kg", "Oxygen", 353.15) if x < 3 else
                                      ("granite_tile_kg", "Granite", 318.15) if x == 3 else
                                      ("cold_cell_kg", "Oxygen", 283.15))
            value = stored(placed[key])
            if not math.isfinite(value) or value <= 0:
                raise ValueError("Invalid frozen F4 placement mass")
            cells[x, y] = ([(descriptions[name], value)], stored(temperature))
    return cells


def f4(arena: Arena):
    hot, cold, divider = rect(1, 2, 1, 2), rect(4, 5, 1, 2), rect(3, 3, 1, 2)
    series = arena.series(arena.ticks["release"])
    room_t = lambda cells, room: sum(m * t for e, m, t in constituents(cells, room) if e["phase"] == "gas") / mass(cells, room, "gas")
    placed = f4_placements(arena)
    first = placed if placed is not None else series[0][1]
    everything = hot + cold + divider
    capacity = sum(m * e["specific_heat_j_per_kg_k"] for e, m, _ in constituents(first, everything) if m > 0)
    heat = sum(m * e["specific_heat_j_per_kg_k"] * t for e, m, t in constituents(first, everything) if m > 0)
    common = heat / capacity
    gas = hot + cold
    final_t = final(series, lambda cells: room_t(cells, gas))
    return {"converged_35k": event(series, lambda cells: room_t(cells, hot) - room_t(cells, cold) <= 35),
            "final_gas_temperature_k": final_t, "common_temperature_k": common, "temperature_error_k": abs(final_t - common),
            "initial_heat_j": heat, "initial_gas_kg": mass(first, gas, "gas"),
            "initial_source": "frozen binary32 placements" if placed is not None else "release sample; frozen placements unavailable",
            "release_sample_heat_j": gas_totals(series[0][1], everything)[1]}


def f5(arena: Arena):
    placed = arena.record["placed"]
    full = {"Water": placed["Water_full_cell_kg"], "CrudeOil": placed["CrudeOil_full_cell_kg"]}
    columns, rows = range(1, 9), range(1, 13)

    def profile(cells, liquid):
        """Volume of the liquid per row (m^3), bottom row first."""
        return [mass(cells, [(x, y) for x in columns], element_id=liquid) / full[liquid] for y in rows]

    def height_below(volumes, fraction):
        total = sum(volumes); need = fraction * total; acc = 0.0
        for k, v in enumerate(volumes):
            if v > 0 and acc + v >= need:
                return k + (need - acc) / v
            acc += v
        return float(len(volumes))

    def centroid(volumes):
        total = sum(volumes)
        return sum((k + .5) * v for k, v in enumerate(volumes)) / total if total > 0 else math.nan

    def columns_height(cells):
        return [sum(m / full[e["id"]] for e, m, _ in constituents(cells, [(x, y) for y in rows]) if e["id"] in full) for x in columns]

    def settled(cells):
        w, o = profile(cells, "Water"), profile(cells, "CrudeOil")
        heights = columns_height(cells)
        return height_below(w, .99) <= height_below(o, .01) and max(heights) - min(heights) <= 1

    series = arena.series(arena.ticks["release"])
    last = series[-1][1]
    water_c, oil_c = centroid(profile(last, "Water")), centroid(profile(last, "CrudeOil"))
    return {"settled": event(series, settled),
            "final_layer_order_bottom_to_top": ["Water", "CrudeOil"] if water_c < oil_c else ["CrudeOil", "Water"],
            "final_mean_depth_m": {"Water": final(series, lambda c: sum(profile(c, "Water")) / 8),
                                   "CrudeOil": final(series, lambda c: sum(profile(c, "CrudeOil")) / 8)},
            "nominal_depth_m": {"Water": 4.0, "CrudeOil": 2.0},
            "interface_overlap_m": max(0.0, height_below(profile(last, "Water"), .99) - height_below(profile(last, "CrudeOil"), .01))}


METRICS = {"f1": f1, "f2": f2, "f3": f3, "f4": f4, "f5": f5}


def load(path: Path) -> Arena:
    record = json.loads(path.read_text())
    rows = [[float(v) for v in line.split()] for line in path.with_suffix(".txt").read_text().splitlines() if line.strip()]
    return Arena(record, rows)


def initial(arena: Arena) -> dict:
    """Initial mass/heat by element; F4 uses prescribed binary32 inputs when available."""
    tick = arena.ticks["locked"] if arena.record["arena"] == "f2" else arena.ticks["release"]
    placed = f4_placements(arena) if arena.record["arena"] == "f4" else None
    first = placed if placed is not None else arena.row_at(tick)
    out = {}
    for element, m, t in constituents(first, list(first)):
        if element["phase"] == "vacuum" or m <= 0:
            continue
        entry = out.setdefault(element["id"], {"kg": 0.0, "heat_j": 0.0, "cells": 0, **{k: v for k, v in element.items() if k != "id"}})
        entry["kg"] += m; entry["heat_j"] += m * element["specific_heat_j_per_kg_k"] * t; entry["cells"] += 1
    result = {"tick": tick, "elements": out, "placed": arena.record.get("placed", {})}
    if placed is not None:
        result["source"] = "frozen binary32 placements; tick marks the release sample"
    return result


def sampling(arena: Arena, legacy_fluid_only_booking=False) -> dict | None:
    """Audit the sampler without changing any frozen scenario metric."""
    audits = arena.record.get("owner_sample_audits")
    if audits is None or arena.record.get("simulation") != "colony owner":
        return None
    if len(audits) != len(arena.rows) or not audits:
        raise ValueError("Owner sample audits do not match the exported rows")
    sample_energies(arena)
    scope = arena.record.get("booked_inventory_scope", "fluid+reservoir")
    if legacy_fluid_only_booking:
        if "booked_inventory_scope" in arena.record:
            raise ValueError("Legacy booking override requires a record without an explicit inventory scope")
        scope = "fluid"
    if scope not in ("fluid", "fluid+reservoir"):
        raise ValueError("Unknown booked inventory scope")
    frames, inventories, refused, residuals = [], [], [], []
    for row, audit in zip(arena.rows, audits):
        if row[0] != audit["tick"]:
            raise ValueError("Owner audit tick does not match its sample")
        frames.append(int(audit["frame"]))
        cells = arena.cells(row)
        total = math.fsum(m for e, m, _ in constituents(cells, list(cells)) if e["phase"] in ("gas", "liquid"))
        inventories.append(math.fsum((total, -audit["injected_kg"], audit["extracted_kg"],
                                      audit["reservoir_kg"], audit["trace_sink_kg"], audit["space_sink_kg"])))
        if "expected_fluid_and_reservoir_kg" in audit:
            held = audit["reservoir_kg"] if scope == "fluid+reservoir" else 0
            residuals.append(math.fsum((total, held, -audit["expected_fluid_and_reservoir_kg"])))
        refused.append(audit["projection_refused_cells"])
    deltas = [b - a for a, b in zip(frames, frames[1:])]
    if any(d < 0 for d in deltas):
        raise ValueError("Owner sample frame went backwards")
    if arena.record.get("owner_sample_history"):
        offset = arena.record["owner_tick_offset"]
        if any(a["tick"] != a["frame"] + offset for a in audits) or any(d != 1 for d in deltas):
            raise ValueError("Retained owner samples are not consecutive on their source clock")
        if any(abs(b["time_s"] - a["time_s"] - arena.dt) > 1e-6 for a, b in zip(audits, audits[1:])):
            raise ValueError("Retained owner sample interval differs from the fixed tick")
    result = {"source": arena.record.get("fluid_sampling"), "samples": len(audits),
            "owner_frame_repeats": deltas.count(0), "owner_frame_skips": sum(max(0, d - 1) for d in deltas),
            "projection_refused_samples": sum(bool(cells) for cells in refused),
            "projection_refused_cell_samples": sum(map(len, refused)),
            "projection_refused_unique_cells": len({cell for cells in refused for cell in cells}),
            "inventory_span_before_identity_conversion_accounting_kg": max(inventories) - min(inventories)}
    if residuals:
        if len(residuals) != len(audits):
            raise ValueError("Some samples lack their full booked inventory")
        result["maximum_booked_inventory_residual_kg"] = max(map(abs, residuals))
        result["booked_inventory_scope"] = scope
        if legacy_fluid_only_booking:
            result["legacy_booking_override"] = "explicit: original export mislabeled fluid-only ledger as fluid+reservoir"
    if any("accepted_external_heat_j" in a for a in audits):
        if not all("accepted_external_heat_j" in a for a in audits):
            raise ValueError("Some samples lack accepted external heat")
        heat = [a["accepted_external_heat_j"] for a in audits]
        result["accepted_external_heat_span_j"] = max(heat) - min(heat)
    return result


def sample_energies(arena: Arena):
    """Read only energy observations aligned with their composition/heat epoch."""
    audits = arena.record.get("owner_sample_audits", [])
    if arena.record.get("simulation") != "colony owner" or not any("energy" in audit for audit in audits):
        return None
    if len(audits) != len(arena.rows) or not all("energy" in audit for audit in audits):
        raise ValueError("Some owner samples lack their energy observation")
    result = []
    session = audits[0]["session"]
    for audit in audits:
        energy = audit["energy"]
        if audit["session"] != session or energy["session"] != session or energy["frame"] != audit["frame"] or energy["time_s"] != audit["time_s"]:
            raise ValueError("Owner energy observation differs from its composition session/frame/time")
        if energy["accepted_external_heat_j"] != audit["accepted_external_heat_j"]:
            raise ValueError("Owner energy and accepted heat observations differ")
        if arena.record["arena"] == "f4":
            if arena.record.get("solid_sampling") != "retained native heat-input ticket paired with the same owner frame" or \
                    audit.get("native_ticket", 0) <= 0 or audit.get("native_sample_ticket") != audit["native_ticket"]:
                raise ValueError("F4 native solids differ from the owner's heat-input ticket")
        fields = ("internal_energy_j", "kinetic_energy_j", "potential_energy_j", "thermal_energy_j",
                  "formation_energy_j", "heat_capacity_j_per_k", "fluid_mass_kg", "accepted_external_heat_j", "trace_sink_energy_j")
        if not all(math.isfinite(energy[key]) for key in fields) or any(energy[key] < 0 for key in ("kinetic_energy_j", "heat_capacity_j_per_k", "fluid_mass_kg")):
            raise ValueError("Invalid owner energy observation")
        result.append(energy)
    return result


def metrics(path: Path, legacy_fluid_only_booking=False) -> dict:
    arena = load(path)
    result = {"arena": arena.record["arena"], "simulation": arena.record["simulation"], "initial": initial(arena),
              "owner": arena.record.get("owner", "none"), **METRICS[arena.record["arena"]](arena)}
    audit = sampling(arena, legacy_fluid_only_booking)
    if audit is not None:
        result["sampling"] = audit
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("record", type=Path, help="floor-<id>.json (its samples beside it as floor-<id>.txt)")
    parser.add_argument("--legacy-fluid-only-booking", action="store_true",
                        help="explicitly interpret old mislabeled booked inventories as fluid-only; leaves the source record unchanged")
    args = parser.parse_args()
    print(json.dumps(metrics(args.record, args.legacy_fluid_only_booking), indent=2))


if __name__ == "__main__":
    main()
