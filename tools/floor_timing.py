#!/usr/bin/env python3
"""Compare floor event brackets over a documented range of possible truth times.

Both measurements and truth remain intervals. A pass requires every game time
in its sample bracket to be no worse than every vanilla time in its bracket,
for every possible truth time. A censored candidate stays unresolved. A
censored vanilla observation supplies only its explicitly derived lower bound.
"""
import argparse
import json
import math


def interval(values):
    if len(values) != 2:
        raise ValueError("An interval needs two endpoints")
    lo, hi = map(float, values)
    if not math.isfinite(lo) or not math.isfinite(hi) or lo < 0 or hi < lo:
        raise ValueError("Event intervals must be finite, nonnegative and ordered")
    return lo, hi


def distance_bounds(measurement, truth):
    lo, hi = measurement
    return max(lo - truth, truth - hi, 0.0), max(abs(lo - truth), abs(hi - truth))


def compare(game, vanilla, truth, basis):
    """Return a conservative comparison; `basis` documents the truth range.

    `game` and `vanilla` are floor_metrics.event records. A universally worse
    game measurement fails. If possible truth/measurement choices reverse the
    comparison, the result remains unresolved, requiring better reference data.
    """
    if not isinstance(basis, str) or not basis.strip():
        raise ValueError("The truth range needs a documented physical basis")
    truth = interval(truth)
    result = {"truth_range_s": list(truth), "truth_basis": basis,
              "game": game, "vanilla": vanilla}
    if game.get("censored") or game.get("unresolved") or vanilla.get("unresolved"):
        return {**result, "status": "unresolved", "reason": "censored or unresolved event"}
    game_range = interval(game["bracket_s"])
    if vanilla.get("censored"):
        if "event_lower_bound_s" not in vanilla:
            return {**result, "status": "unresolved", "reason": "censored vanilla lacks an explicit event lower bound"}
        lower = interval([vanilla["event_lower_bound_s"], vanilla["event_lower_bound_s"]])[0]
        observed = interval([0, vanilla["observed_s"]])[1]
        if lower > observed:
            raise ValueError("Censored event lower bound exceeds its observation window")
        points = {truth[0], truth[1]}
        points.update(value for value in (*game_range, game_range[0] + (game_range[1] - game_range[0]) / 2, lower)
                      if truth[0] <= value <= truth[1])
        # The unknown vanilla event lies in [lower,+infinity). Its minimum
        # possible error is max(lower-truth,0); its maximum is unbounded. The
        # candidate can pass only if its worst error beats that lower error
        # bound everywhere. No finite vanilla event time is invented.
        worst = max(distance_bounds(game_range, value)[1] - max(lower - value, 0) for value in points)
        return {**result, "status": "pass" if worst <= 0 else "unresolved",
                "vanilla_event_lower_bound_s": lower,
                "game_minus_vanilla_absolute_error_upper_bound_s": worst,
                "reason": "comparison uses only the censored vanilla error lower bound"}
    vanilla_range = interval(vanilla["bracket_s"])
    # Distance to an interval changes slope at its endpoints; maximum
    # distance changes slope at its midpoint. Their differences are linear
    # between these breakpoints, so examining them and the truth endpoints
    # covers the whole continuous truth range without sampling it.
    points = {truth[0], truth[1]}
    for values in (game_range, vanilla_range):
        points.update(value for value in (*values, values[0] + (values[1] - values[0]) / 2)
                      if truth[0] <= value <= truth[1])
    differences = []
    for value in sorted(points):
        game_min, game_max = distance_bounds(game_range, value)
        vanilla_min, vanilla_max = distance_bounds(vanilla_range, value)
        differences.append((game_min - vanilla_max, game_max - vanilla_min))
    best = min(pair[0] for pair in differences)
    worst = max(pair[1] for pair in differences)
    status = "pass" if worst <= 0 else "fail" if best > 0 else "unresolved"
    return {**result, "status": status,
            "game_minus_vanilla_absolute_error_range_s": [best, worst]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", nargs=2, type=float, required=True, metavar=("LOW", "HIGH"))
    vanilla = parser.add_mutually_exclusive_group(required=True)
    vanilla.add_argument("--vanilla", nargs=2, type=float, metavar=("LOW", "HIGH"))
    vanilla.add_argument("--vanilla-censored", nargs=2, type=float, metavar=("EVENT_LOWER_BOUND", "OBSERVED_TO"),
                         help="explicit lower bound accounting for an unconfirmed persistence tail, and observation endpoint")
    parser.add_argument("--truth", nargs=2, type=float, required=True, metavar=("LOW", "HIGH"))
    parser.add_argument("--basis", required=True, help="Physical justification or independent reference provenance")
    args = parser.parse_args()
    try:
        measurement = {"bracket_s": args.vanilla} if args.vanilla is not None else {
            "censored": True, "event_lower_bound_s": args.vanilla_censored[0], "observed_s": args.vanilla_censored[1]}
        result = compare({"bracket_s": args.game}, measurement, args.truth, args.basis)
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
