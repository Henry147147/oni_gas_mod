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
                out[(x, y)] = (parts, row[k]); k += 1
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
    out, _ = gas_rooms(arena, t["release"], left, right, left + right + door)
    out.update(closed_hold_leak_fraction=leak, command_to_open_s=(t["opened"] - t["commanded"]) * arena.dt)
    return out


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


def f4(arena: Arena):
    hot, cold, divider = rect(1, 2, 1, 2), rect(4, 5, 1, 2), rect(3, 3, 1, 2)
    series = arena.series(arena.ticks["release"])
    room_t = lambda cells, room: sum(m * t for e, m, t in constituents(cells, room) if e["phase"] == "gas") / mass(cells, room, "gas")
    first = series[0][1]
    everything = hot + cold + divider
    capacity = sum(m * e["specific_heat_j_per_kg_k"] for e, m, _ in constituents(first, everything) if m > 0)
    heat = sum(m * e["specific_heat_j_per_kg_k"] * t for e, m, t in constituents(first, everything) if m > 0)
    common = heat / capacity
    gas = hot + cold
    final_t = final(series, lambda cells: room_t(cells, gas))
    return {"converged_35k": event(series, lambda cells: room_t(cells, hot) - room_t(cells, cold) <= 35),
            "final_gas_temperature_k": final_t, "common_temperature_k": common, "temperature_error_k": abs(final_t - common),
            "initial_heat_j": heat, "initial_gas_kg": mass(first, gas, "gas")}


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
    """The t = 0 sample (F2: the start of the closed hold): mass and heat (m c T) per element, and each material's properties."""
    tick = arena.ticks["locked"] if arena.record["arena"] == "f2" else arena.ticks["release"]
    first = arena.row_at(tick)
    out = {}
    for element, m, t in constituents(first, list(first)):
        if element["phase"] == "vacuum" or m <= 0:
            continue
        entry = out.setdefault(element["id"], {"kg": 0.0, "heat_j": 0.0, "cells": 0, **{k: v for k, v in element.items() if k != "id"}})
        entry["kg"] += m; entry["heat_j"] += m * element["specific_heat_j_per_kg_k"] * t; entry["cells"] += 1
    return {"tick": tick, "elements": out, "placed": arena.record.get("placed", {})}


def metrics(path: Path) -> dict:
    arena = load(path)
    return {"arena": arena.record["arena"], "simulation": arena.record["simulation"], "initial": initial(arena),
            "owner": arena.record.get("owner", "none"), **METRICS[arena.record["arena"]](arena)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("record", type=Path, help="floor-<id>.json (its samples beside it as floor-<id>.txt)")
    print(json.dumps(metrics(parser.parse_args().record), indent=2))


if __name__ == "__main__":
    main()
