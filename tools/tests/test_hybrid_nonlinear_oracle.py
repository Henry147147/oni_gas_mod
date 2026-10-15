"""Independent-oracle calibration on known mathematical data, no native runs."""
from fractions import Fraction as F
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_hybrid_nonlinear_oracle as oracle


def encoded(value):
    return struct.pack(">d", float(value)).hex()


class NonlinearOracleCalibration(unittest.TestCase):
    def encloses(self, bound, exact):
        self.assertLessEqual(bound.lo, exact)
        self.assertGreaterEqual(bound.hi, exact)

    def test_ieee_subnormal_signed_zero_and_nonfinite_are_not_decimal_substituted(self):
        self.assertEqual(oracle.bits("0000000000000001"), F(1, 2**1074))
        self.assertEqual(oracle.bits("8000000000000000"), 0)
        self.assertEqual(oracle.bits("3ff0000000000001"), 1+F(1, 2**52))
        for malformed in ("7ff0000000000000", "7ff8000000000001", "not-a-binary-value", 1):
            with self.subTest(malformed=malformed), self.assertRaises(oracle.GateFailure):
                oracle.bits(malformed)

    def test_exact_full_and_half_polynomial_integrals(self):
        self.assertEqual(oracle.integrate(oracle.RHO), F(7, 6))
        self.assertEqual(oracle.integrate(oracle.RHO, F(0), F(1, 2)), F(103, 192))
        self.assertEqual(sum(oracle.integrate(p) for p in oracle.SPECIES), F(7, 6))
        self.assertEqual(oracle.integrate(oracle.SPECIES[2]), F(379, 240*2**40))
        # This exact trace identity fails if the tag primitive is reused or if
        # a bulk-sized epsilon is used to erase a positive small coefficient.
        self.assertGreater(oracle.integrate(oracle.SPECIES[2]), F(0))

    def test_declared_integer_sampling_coefficients_match_independent_products(self):
        b43, b42, b39, b37 = 2**43, 2**42, 2**39, 2**37
        integer = ((3*b43-32, b42-24, 3*b39-16, -b39-4, -b37-1),
                   (b43, b42, 5*b39, b39, b37), (32, 24, 16, 4, 1))
        for s in range(3):
            self.assertEqual(tuple(F(x, 2**45) for x in integer[s]), oracle.SPECIES[s])
        for denominator in (32, 64, 128, 256):
            for a, b in ((0, 4), (5, 11), (denominator-9, denominator)):
                exact = oracle.integrate(oracle.SPECIES[2], F(a, denominator), F(b, denominator))
                self.assertEqual(oracle.rounded(exact), F.from_float(float(exact)))

    def test_bernstein_convex_hull_and_translation(self):
        p = (F(3), F(-4), F(2))
        self.assertEqual(oracle.bernstein(p, F(0), F(1)), [F(3), F(1), F(1)])
        for a, b in ((F(0), F(1)), (F(1, 4), F(3, 4))):
            coefficients = oracle.bernstein(p, a, b)
            for k in range(17):
                value = oracle.at(p, a+(b-a)*F(k, 16))
                self.assertLessEqual(min(coefficients), value)
                self.assertGreaterEqual(max(coefficients), value)

    def test_absolute_integral_known_linear_crossing_and_rho_weight(self):
        p = (F(-1, 2), F(1))
        self.encloses(oracle.absolute_integral(p, (F(1),), F(0), F(1)), F(1, 4))
        # Split analytic integral, with a deliberately nonconstant weight.
        exact = -oracle.integrate(oracle.mul(p, oracle.RHO), F(0), F(1, 2))+oracle.integrate(oracle.mul(p, oracle.RHO), F(1, 2), F(1))
        self.encloses(oracle.absolute_integral(p, oracle.RHO, F(0), F(1)), exact)

    def test_double_root_near_endpoint_and_two_crossings_are_enclosed(self):
        for root in (F(0), F(1, 2**45), F(3, 7), F(1)):
            square = oracle.mul((-root, F(1)), (-root, F(1)))
            exact = oracle.integrate(square)
            self.encloses(oracle.absolute_integral(square, (F(1),), F(0), F(1)), exact)
        p = oracle.mul((F(-1, 4), F(1)), (F(-3, 4), F(1)))
        exact = oracle.integrate(p, F(0), F(1, 4))-oracle.integrate(p, F(1, 4), F(3, 4))+oracle.integrate(p, F(3, 4), F(1))
        self.encloses(oracle.absolute_integral(p, (F(1),), F(0), F(1)), exact)

    def test_trace_weight_scaling_does_not_introduce_unit_error_floor(self):
        p = (F(-1, 3), F(1))
        a = oracle.absolute_integral(p, (F(1),), F(0), F(1))
        b = oracle.absolute_integral(tuple(x/F(2**900) for x in p), (F(1),), F(0), F(1))
        self.encloses(b, F(5, 18*2**900))
        self.assertEqual(b.lo, a.lo/F(2**900))
        self.assertEqual(b.hi, a.hi/F(2**900))

    def test_supremum_cubic_and_interior_extremum(self):
        self.encloses(oracle.supremum((F(0), F(1), F(-1)), F(0), F(1)), F(1, 4))
        self.encloses(oracle.supremum((F(0), F(0), F(0), F(1)), F(0), F(1)), F(1))
        zero = oracle.supremum((F(0),), F(0), F(1))
        self.assertEqual(zero, oracle.Bound.exact(F(0)))

    def test_unequal_node_union_has_exact_weighted_integral_and_supremum(self):
        rising = ([F(0), F(1, 4), F(1)], [F(0), F(1, 4), F(1)])
        falling = ([F(0), F(1, 2), F(3, 4), F(1)], [F(1), F(1, 2), F(1, 4), F(0)])
        l1, sup = oracle.pl_union(rising, falling)
        polynomial = (F(-1), F(2))
        exact = -oracle.integrate(oracle.mul(polynomial, oracle.RHO), F(0), F(1, 2))+oracle.integrate(oracle.mul(polynomial, oracle.RHO), F(1, 2), F(1))
        self.encloses(l1, exact)
        self.assertEqual(sup, oracle.Bound.exact(F(1)))

    def test_unequal_cell_union_positive_and_tiny_species(self):
        a = ([F(0), F(1, 2), F(1)], [F(0), F(2)])
        b = ([F(0), F(1, 4), F(3, 4), F(1)], [F(1), F(1), F(1)])
        self.assertEqual(oracle.cell_union(a, b), oracle.Bound.exact(F(1)))
        small = (a[0], [x/F(2**900) for x in a[1]])
        smallb = (b[0], [x/F(2**900) for x in b[1]])
        self.assertEqual(oracle.cell_union(small, smallb), oracle.Bound.exact(F(1, 2**900)))

    def test_reflected_state_transformation_preserves_material_order(self):
        state = {"nodes": [{"q": encoded(q), "u": encoded(u), "I": encoded(i)} for q, u, i in ((0, -2, 1), (.25, -1, 2), (1, 0, 3))], "cells": ["left", "right"]}
        q, u, inertia, cells = oracle.state_arrays(state, True)
        self.assertEqual(q, [F(0), F(3, 4), F(1)])
        self.assertEqual(u, [F(0), F(1), F(2)])
        self.assertEqual(inertia, [F(3), F(2), F(1)])
        self.assertEqual(cells, ["right", "left"])

    def test_geometry_real_seven_seven_eight_seven_and_equal_count_ids(self):
        c = {"N": 8, "d": 4, "phase": 2, "carrier": 1, "reverse": False}
        parts = [oracle.expected_geometry(c, F(k, 32)) for k in range(4)]
        self.assertEqual([len(q)-1 for q, _, _ in parts], [7, 7, 8, 7])
        self.assertNotEqual(parts[0][1], parts[3][1])
        self.assertEqual(parts[2][0], [F(0), F(1, 8), F(1, 4), F(3, 8), F(1, 2), F(5, 8), F(3, 4), F(7, 8), F(1)])

    def test_matrix_exact_288_nonlinear_and_16_controls(self):
        keys = oracle.expected_keys()
        self.assertEqual(sum(not key[-1] for key in keys), 288)
        self.assertEqual(sum(key[-1] for key in keys), 16)
        self.assertEqual(len(keys), 304)

    def coverage_fixture(self, width=F(1, 16), two_records=True):
        if two_records:
            grid = [encoded(width/2), encoded(0), encoded(width), -1, 2]
            rows = [[0, 0, -1, encoded(0), encoded(width/2), encoded(width/2), encoded(width/2)],
                    [0, 0, 0, encoded(width/2), encoded(width), encoded(width/2), encoded(width/2)]]
        else:
            grid = [encoded(0), encoded(0), encoded(width), 0, 1]
            rows = [[0, 0, 0, encoded(0), encoded(width), encoded(width), encoded(width)]]
        # Independent known dyadic value: two equal-width64eps terms sum to
        #128eps*width even when the interval has only one coverage record.
        bound = 128*oracle.EPS*width
        balances = [[0, 0, len(rows), encoded(width), encoded(width), encoded(0), encoded(bound),
                     encoded(width), encoded(width), encoded(0), encoded(bound)]]
        return [F(0), width], grid, rows, balances

    def test_accepted_two_term_coverage_certificate_one_two_records_and_tiny_width(self):
        for width in (F(1, 16), F(1, 2**900)):
            for two_records in (False, True):
                with self.subTest(width=width, two_records=two_records):
                    q, grid, rows, balances = self.coverage_fixture(width, two_records)
                    oracle.verify_coverage(q, grid, rows, balances, 0)
                    self.assertEqual(oracle.coverage_certificate(width, width), 128*oracle.EPS*width)
        self.assertEqual(oracle.coverage_certificate(F(1, 16), F(1, 8)), 64*oracle.EPS*F(3, 16))

    def test_corrupt_one_term_zero_negative_and_nonlocal_coverage_certificates_rejected(self):
        for width in (F(1, 16), F(1, 2**900)):
            wrong = (64*oracle.EPS*width, F(0), -128*oracle.EPS*width,
                     F(1), (128*oracle.EPS*width)*(1+oracle.EPS))
            for bound in wrong:
                for index in (6, 10):
                    with self.subTest(width=width, bound=bound, index=index), self.assertRaises(oracle.GateFailure):
                        q, grid, rows, balances = self.coverage_fixture(width)
                        balances[0][index] = encoded(bound)
                        oracle.verify_coverage(q, grid, rows, balances, 0)

    def test_certificate_cannot_repair_wrong_overlap_coverage_or_residual(self):
        for defect in ("missing_overlap", "wrong_width", "nonzero_residual"):
            q, grid, rows, balances = self.coverage_fixture()
            if defect == "missing_overlap":
                rows.pop()
            elif defect == "wrong_width":
                rows[0][5] = encoded(F(1, 16))
            else:
                balances[0][5] = encoded(F(1, 2**55))
            with self.subTest(defect=defect), self.assertRaises(oracle.GateFailure):
                oracle.verify_coverage(q, grid, rows, balances, 0)

    def test_exact_zero_event_prefix_later_two_four_events_with_real_geometry_motion(self):
        for reflected in (False, True):
            c = {"N": 16, "d": 4, "phase": 1, "carrier": -1, "reverse": True, "reflected": reflected}
            old_q, old_ids, _ = oracle.expected_geometry(c, F(0))
            old_count = len(old_q)-1
            counters, categories = (0, 0), {}
            flags = (False, False, True, True, True, True, False, False)
            totals = (0, 0, 1, 2, 3, 4, 4, 4)
            for step in range(1, 9):
                tau = F(step, 64)
                q, ids, _ = oracle.expected_geometry(c, tau)
                if step == 1:
                    self.assertNotEqual(q, old_q)  # Actual rezone, despite no count/ID event.
                    self.assertEqual(ids, old_ids)
                    self.assertEqual(len(q)-1, old_count)
                counters = oracle.authenticate_event_counters(counters, old_count, old_ids, len(q)-1, ids,
                                                              flags[step-1], False, totals[step-1], 0)
                categories = oracle.first_event_categories(categories, step, tau, True, old_count, old_ids, len(q)-1, ids)
                if step in (2, 4, 6, 8):
                    self.assertEqual(counters, ((0, 2, 4, 4)[step//2-1], 0))
                old_q, old_ids, old_count = q, ids, len(q)-1
            self.assertEqual(categories, {0: 3, 1: 4, 3: 5})

    def test_corrupt_cumulative_event_equal_counters_and_flags_are_rejected(self):
        old, new = [2, 3, 4], [1, 2, 3, 4]
        valid = (True, False, 1, 0)
        self.assertEqual(oracle.authenticate_event_counters((0, 0), 7, old, 8, new, *valid), (1, 0))
        for flags in ((False, False, 1, 0), (True, True, 1, 0), (True, False, 0, 0),
                      (True, False, 2, 0), (True, False, 1, 1), (True, False, -1, 0),
                      (True, False, True, 0), (1, False, 1, 0)):
            with self.subTest(flags=flags), self.assertRaises(oracle.GateFailure):
                oracle.authenticate_event_counters((0, 0), 7, old, 8, new, *flags)
        self.assertEqual(oracle.authenticate_event_counters((0, 0), 15, old, 15, old, False, False, 0, 0), (0, 0))
        with self.assertRaises(oracle.GateFailure):
            oracle.authenticate_event_counters((0, 0), 15, old, 15, old, False, False, 1, 0)

    def test_equal_count_only_phase_history_has_no_insertion_or_retirement_category(self):
        for reverse in (False, True):
            c = {"N": 32, "d": 2, "phase": 1, "carrier": -1, "reverse": reverse}
            old_q, old_ids, _ = oracle.expected_geometry(c, F(0))
            old_count, counters, categories = len(old_q)-1, (0, 0), {}
            self.assertEqual(old_count, 31)
            # Known integer-phase crossings for these exact sampled phases.
            event_steps = {2, 4, 5, 7} if reverse else {2, 4, 6, 8}
            total = 0
            for step in range(1, 9):
                tau = F(step, 64)
                q, ids, _ = oracle.expected_geometry(c, tau)
                self.assertEqual(len(q)-1, 31)
                event = step in event_steps
                total += int(event)
                counters = oracle.authenticate_event_counters(counters, old_count, old_ids, len(q)-1, ids,
                                                              event, event, total, total)
                categories = oracle.first_event_categories(categories, step, tau, reverse, old_count, old_ids, len(q)-1, ids)
                old_ids, old_count = ids, len(q)-1
            self.assertEqual(counters, (4, 4))
            self.assertEqual(categories, {2: 2, 3: 5} if reverse else {2: 2})

    def test_first_event_category_is_not_replaced_by_a_later_occurrence(self):
        first = oracle.first_event_categories({}, 3, F(3, 64), False, 15, [2, 3], 16, [1, 2, 3])
        self.assertEqual(first, {0: 3})
        later = oracle.first_event_categories(first, 7, F(7, 64), False, 15, [2, 3], 16, [1, 2, 3])
        self.assertEqual(later, {0: 3})
        self.assertEqual(first, {0: 3})  # Input certificate remains immutable.

    def test_primal_corner_union_and_independent_given_rational_mechanics(self):
        qa = list(map(F, (0, 1, 2, 3, 4)))
        qb = [F(0), F(1), F(5, 2), F(3), F(4)]
        masses = list(map(F, (1, 3, 1, 1)))
        velocity = list(map(F, (-2, 2, 4, -1, 3)))
        da, db = oracle.corners(qa), oracle.corners(qb)
        pieces = [[F(0)]*3 for _ in range(8)]
        for i, j, left, right in oracle.overlaps(da, db):
            cell, side = divmod(i, 2)
            mass = masses[cell]/2*(right-left)/(da[i+1]-da[i])
            u = velocity[cell+side]
            for k, amount in enumerate((mass, mass*u, mass*u*u/2)):
                pieces[j][k] += amount
        newm = [F(1), F(7, 2), F(1, 2), F(1)]
        balanced = []
        for i in range(4):
            left, right, _ = oracle.balance_half(pieces[2*i], pieces[2*i+1], newm[i])
            balanced += [left, right]
        self.assertEqual(balanced[0], [F(1, 2), F(-1), F(1)])
        self.assertEqual(balanced[-1], [F(1, 2), F(3, 2), F(9, 4)])
        nodes = []
        for j in range(5):
            incident = ([] if j == 0 else [balanced[2*j-1]])+([] if j == 4 else [balanced[2*j]])
            inertia, momentum, h = [sum(x[k] for x in incident) for k in range(3)]
            nodes.append((inertia, momentum, h-momentum*momentum/(2*inertia)))
        self.assertEqual(nodes[1], (F(9, 4), F(17, 3), F(70, 81)))
        self.assertEqual(nodes[1][1]/nodes[1][0], F(68, 27))
        self.assertEqual(nodes[1][2]*F(1, 2)/nodes[1][0], F(140, 729))
        self.assertEqual(nodes[2][2], F(1655, 576))
        self.assertEqual(sum(n[2] for n in nodes), F(19375, 5184))
        self.assertEqual(sum(n[1] for n in nodes), F(23, 2))
        newk = sum(n[1]*n[1]/(2*n[0]) for n in nodes)
        self.assertEqual(newk, F(103745, 5184))
        self.assertEqual(newk+sum(n[2] for n in nodes), F(95, 4))

    def test_decay_is_strict_enclosure_and_failure_is_not_rounded_to_pass(self):
        oracle.compare_decay(oracle.Bound(F(7, 10), F(71, 100)), oracle.Bound(F(1), F(11, 10)), F(4, 5), "fixture", {})
        for fine in (oracle.Bound.exact(F(4, 5)), oracle.Bound(F(7, 10), F(81, 100)), oracle.Bound.exact(F(0))):
            coarse = oracle.Bound.exact(F(0)) if fine.hi == 0 else oracle.Bound.exact(F(1))
            with self.assertRaises(oracle.GateFailure):
                oracle.compare_decay(fine, coarse, F(4, 5), "deliberate failure", {})

    def test_trace_local_arithmetic_cannot_accept_bulk_floor(self):
        trace = F(1, 2**900)
        oracle.scalar_close(trace, trace, 128, trace, "exact trace")
        with self.assertRaises(oracle.GateFailure):
            oracle.scalar_close(F(0), trace, 128, trace, "dropped trace")

    def gas_fixture(self, volume=F(1), trace=None):
        masses = [volume/2, volume/4, volume/F(2**42) if trace is None else trace]
        molar = [F.from_float(x) for x in (.0280134, .0319988, .0440095)]
        n = sum((m/w for m, w in zip(masses, molar)), F(0))
        cell = {"V": encoded(volume), "gasV": encoded(volume), "volume_residual": encoded(0),
                "liquidV": encoded(0), "vacuumV": encoded(0), "hasgas": True, "hasvacuum": False,
                "T": encoded(300), "p": encoded(n*oracle.R*300/volume), "m": list(map(encoded, masses))}
        return cell, [{"molar": encoded(w)} for w in molar]

    def test_exact_output_preimages_at_binade_zero_subnormal_and_finite_limit(self):
        self.assertEqual(oracle.rounding_preimage(F(1)), (1-F(1, 2**54), 1+F(1, 2**53)))
        self.assertEqual(oracle.rounding_preimage(F(-1)), (-1-F(1, 2**53), -1+F(1, 2**54)))
        eta = F(1, 2**1074)
        self.assertEqual(oracle.rounding_preimage(F(0)), (-eta/2, eta/2))
        self.assertEqual(oracle.rounding_preimage(eta), (eta/2, 3*eta/2))
        maximum = F.from_float(sys.float_info.max)
        low, high = oracle.rounding_preimage(maximum)
        self.assertEqual(maximum-low, high-maximum)
        self.assertLess(low, maximum)
        self.assertGreater(high, maximum)
        self.assertEqual(oracle.operation_roundoff(F(0)), 0)
        self.assertEqual(oracle.operation_roundoff(eta/4), eta/2)
        self.assertEqual(oracle.operation_roundoff(F(1)), F(1, 2**53))
        with self.assertRaises(oracle.GateFailure):
            oracle.rounding_preimage(F(1, 3))
        with self.assertRaises(oracle.GateFailure):
            oracle.operation_roundoff(2*maximum)

    def test_three_mole_enclosure_covers_independent_rounded_compensated_sums(self):
        # These use an explicit binary64 simulator only to calibrate the
        # derived error bound; the native output is never imported or rerun.
        eta = F(1, 2**1074)
        sets = (([F(1), F(1, 2**53), F(1, 2**53)], [F(1)]*3),
                ([F(1, 2), F(1, 4), eta], [F.from_float(x) for x in (.0280134, .0319988, .0440095)]),
                ([F(3, 8), F(5, 16), F(1, 2**42)], [F(3, 8), F(5, 4), F(7, 32)]))
        for scale in (F(1), F(1, 2**900), F(2**900)):
            for masses, molar in sets:
                # Keep the one denormal calibration in its actual range;
                # multiplying it down would change the declared positive input.
                if masses[-1] == eta and scale != 1:
                    continue
                m = [x*scale for x in masses]
                exact, error = oracle.three_positive_moles(m, molar)
                value = correction = F(0)
                for mass, weight in zip(m, molar):
                    item = oracle.rounded(mass/weight)
                    following = oracle.rounded(value+item)
                    residual = (oracle.rounded(oracle.rounded(value-following)+item) if abs(value) >= abs(item)
                                else oracle.rounded(oracle.rounded(item-following)+value))
                    correction = oracle.rounded(correction+residual)
                    value = following
                observed = oracle.rounded(value+correction)
                self.assertLessEqual(abs(observed-exact), error)
                self.assertGreater(exact-error, 0)
        exact, _ = oracle.three_positive_moles(sets[0][0], sets[0][1])
        self.assertEqual(exact, 1+F(1, 2**52))  # Both lost naive addends matter.
        for masses, molar in (([F(1), F(2)], [F(1), F(1)]),
                              ([F(1), F(2), F(0)], [F(1)]*3),
                              ([F(1), F(2), F(3)], [F(1), F(-1), F(1)])):
            with self.subTest(masses=masses, molar=molar), self.assertRaises(oracle.GateFailure):
                oracle.three_positive_moles(masses, molar)

    def test_signed_plus_minus_one_ulp_gas_volume_and_exact_residual_are_valid(self):
        for volume in (F(7, 64), F(1), F(1, 2**900), F(2**900)):
            for direction in (-math.inf, math.inf):
                with self.subTest(volume=volume, direction=direction):
                    cell, profiles = self.gas_fixture(volume)
                    gas = F.from_float(math.nextafter(float(volume), direction))
                    cell.update(gasV=encoded(gas), volume_residual=encoded(gas-volume))
                    certificate = oracle.gas_volume_certificate(cell, profiles)
                    self.assertEqual(certificate["signed_published_conversion_defect"], 0)
                    lo, hi = certificate["common_internal_gas_volume"]
                    self.assertLessEqual(lo, gas)
                    self.assertGreaterEqual(hi, gas)

    def test_actual_preserved_r3_one_ulp_state_has_consistent_negative_residual(self):
        cell = {"V": "3fbc000000000000", "gasV": "3fbbffffffffffff", "volume_residual": "bc70000000000000",
                "liquidV": encoded(0), "vacuumV": encoded(0), "hasgas": True, "hasvacuum": False,
                "T": "4072c000000eccaf", "p": "40f5d353b81d4a85",
                "m": ["3fb58386bfbf646a", "3f9e25e1c19dfb20", "3d3f3ec45314f3cf"]}
        profiles = [{"molar": x} for x in ("3f9caf8b7366292a", "3fa062258f05c1e1", "3fa68869c66d373b")]
        self.assertEqual(oracle.bits(cell["volume_residual"]), -F(1, 2**56))
        self.assertEqual(oracle.gas_volume_certificate(cell, profiles)["signed_published_conversion_defect"], 0)

    def test_separate_gas_and_residual_conversions_need_not_have_zero_published_defect(self):
        for direction in (-math.inf, math.inf):
            cell, profiles = self.gas_fixture()
            gas = F.from_float(math.nextafter(1.0, direction))
            internal = 1+F(3, 4)*(gas-1)
            cell.update(gasV=encoded(gas), volume_residual=encoded(internal-1))
            certificate = oracle.gas_volume_certificate(cell, profiles)
            self.assertNotEqual(certificate["signed_published_conversion_defect"], 0)
            low, high = certificate["common_internal_gas_volume"]
            self.assertLessEqual(low, internal)
            self.assertGreaterEqual(high, internal)

    def test_gas_certificate_encloses_higher_precision_R_literal_rounding_preimage(self):
        cell, profiles = self.gas_fixture()
        native_literal = F("8.31446261815324")
        low, high = oracle.rounding_preimage(oracle.R)
        self.assertLessEqual(low, native_literal)
        self.assertGreaterEqual(high, native_literal)
        moles = sum((oracle.bits(m)/oracle.bits(p["molar"]) for m, p in zip(cell["m"], profiles)), F(0))
        internal = moles*native_literal*oracle.bits(cell["T"])/oracle.bits(cell["p"])
        cell.update(gasV=encoded(internal), volume_residual=encoded(internal-1))
        low, high = oracle.gas_volume_certificate(cell, profiles)["common_internal_gas_volume"]
        self.assertLessEqual(low, internal)
        self.assertGreaterEqual(high, internal)

    def test_corrupt_signed_residual_material_gap_and_false_phase_cannot_use_local_envelope(self):
        for volume in (F(7, 64), F(1, 2**900), F(2**900)):
            for direction in (-math.inf, math.inf):
                for defect in ("zero_residual", "opposite_residual", "material_gap"):
                    cell, profiles = self.gas_fixture(volume)
                    gas = (oracle.rounded(volume*F(101, 100)) if defect == "material_gap"
                           else F.from_float(math.nextafter(float(volume), direction)))
                    residual = gas-volume
                    if defect == "zero_residual":
                        residual = F(0)
                    elif defect == "opposite_residual":
                        residual = -residual
                    cell.update(gasV=encoded(gas), volume_residual=encoded(residual))
                    with self.subTest(volume=volume, direction=direction, defect=defect), self.assertRaises(oracle.GateFailure):
                        oracle.gas_volume_certificate(cell, profiles)
        for field, value in (("hasgas", False), ("hasvacuum", True), ("hasgas", 1),
                             ("liquidV", "0000000000000001"), ("vacuumV", "0000000000000001"),
                             ("p", encoded(0)), ("p", encoded(-1)), ("p", "7ff0000000000000"),
                             ("T", encoded(0)), ("V", encoded(0)), ("gasV", encoded(0))):
            cell, profiles = self.gas_fixture()
            cell[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(oracle.GateFailure):
                oracle.gas_volume_certificate(cell, profiles)

    def test_gas_certificate_scales_locally_without_unit_or_bulk_trace_floor(self):
        base = None
        for scale in (F(1), F(1, 2**900), F(2**900)):
            cell, profiles = self.gas_fixture(scale)
            gas = F.from_float(math.nextafter(float(scale), math.inf))
            cell.update(gasV=encoded(gas), volume_residual=encoded(gas-scale))
            certificate = oracle.gas_volume_certificate(cell, profiles)
            if base is None:
                base = certificate
            for quantity in ("moles_exact", "moles_arithmetic_bound", "local_output_conversion_bound"):
                self.assertEqual(certificate[quantity], base[quantity]*scale)
            self.assertEqual(certificate["reconstructed_internal_gas_volume"],
                             tuple(x*scale for x in base["reconstructed_internal_gas_volume"]))
        cell, profiles = self.gas_fixture(trace=F(1, 2**1074))
        self.assertGreater(oracle.bits(cell["m"][-1]), 0)
        oracle.gas_volume_certificate(cell, profiles)

    def test_exact_negative_arithmetic_heat_control_is_a_magnitude_norm_only(self):
        history = {"Q": "bca5c00000000000", "Qarith": "bc58000000000000", "Kbound": "3d92ac92aaaaaaab"}
        before = dict(history)
        self.assertEqual(oracle.bits(history["Q"]), -F(87, 2**59))
        self.assertEqual(oracle.heat_rate_norm(history, True), oracle.Bound.exact(F(87, 2**56)))
        self.assertEqual(history, before)  # The physical Q/Qarith are not rewritten.
        self.assertLess(oracle.bits(history["Q"]), 0)
        self.assertLess(oracle.bits(history["Qarith"]), 0)

    def test_uniform_heat_norm_both_signs_zero_and_nonuniform_definition_unchanged(self):
        for q in (F(-1, 2**900), F(0), F(1, 2**900)):
            history = {"Q": encoded(q), "Kbound": encoded(abs(q))}
            self.assertEqual(oracle.heat_rate_norm(history, True), oracle.Bound.exact(abs(q)/oracle.TIME))
        for q in (F(0), F(1, 2**900), F(1, 4)):
            history = {"Q": encoded(q), "Kbound": encoded(0)}
            self.assertEqual(oracle.heat_rate_norm(history, False), oracle.Bound.exact(q/oracle.TIME))
        with self.assertRaises(oracle.GateFailure):
            oracle.heat_rate_norm({"Q": encoded(-F(1, 2**900)), "Kbound": encoded(1)}, False)

    def test_out_of_budget_negative_and_positive_uniform_heat_are_rejected(self):
        for bound in (F(0), F(1, 2**900), F(1, 2**50)):
            outside = (F(1, 2**1074) if bound == 0 else bound*(1+oracle.EPS))
            for sign in (-1, 1):
                history = {"Q": encoded(sign*outside), "Kbound": encoded(bound)}
                with self.subTest(bound=bound, sign=sign), self.assertRaises(oracle.GateFailure) as caught:
                    oracle.heat_rate_norm(history, True)
                self.assertEqual(caught.exception.evidence, {"signed_Q": sign*outside, "Kbound": bound})

    def test_invalid_nonnegative_bound_reports_its_exact_endpoints(self):
        for lo, hi in ((F(-87, 2**56), F(-87, 2**56)), (F(1), F(0))):
            with self.subTest(lo=lo, hi=hi), self.assertRaises(oracle.GateFailure) as caught:
                oracle.Bound(lo, hi)
            self.assertEqual(caught.exception.evidence, {"lo": lo, "hi": hi})

    def test_current_sample_is_embedded_in_preserved_first_failure(self):
        import json
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)/"native"
            case = root/"synthetic-wrong-geometry"
            case.mkdir(parents=True)
            (root/"COMPLETE.json").write_text(json.dumps({"schema": 1, "cases": 304, "nonlinear": 288,
                "uniform": 16, "native_physical_checks": True, "independent_oracle_required": True, "whole_A2": False}))
            config = {"N": 16, "d": 4, "phase": 0, "carrier": 1, "reverse": False, "reflected": False, "uniform": False}
            state = {"nodes": [{"q": encoded(q), "u": encoded(1), "I": encoded(1)} for q in (0, 1)], "cells": []}
            first = None
            for number in range(5):
                sample = {"config": config, "tau": encoded(number*oracle.TIME/4), "state": state}
                (case/f"sample-{number}.json").write_text(json.dumps(sample))
                if number == 0:
                    first = sample
            output = Path(temporary)/"oracle"
            self.assertEqual(oracle.main(["--input", str(root), "--output", str(output)]), 1)
            failure = json.loads((output/"FIRST_FAILURE.json").read_text())
            self.assertEqual(failure["context"]["sample_file"], str(case/"sample-0.json"))
            self.assertEqual(failure["complete_actual_states_and_packets"]["sample_file"], first)
            self.assertIn("Actual partition differs", failure["error"])

    def test_incomplete_native_output_preserves_first_failure_and_never_accepts_old_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)/"native"
            root.mkdir()
            (root/"FIRST_FAILURE.json").write_text('{"failure":"synthetic native failure"}')
            output = Path(temporary)/"oracle"
            self.assertEqual(oracle.main(["--input", str(root), "--output", str(output)]), 1)
            self.assertTrue((output/"FIRST_FAILURE.json").is_file())
            self.assertFalse(__import__("json").loads((output/"summary.json").read_text())["oracle_passed"])
            with self.assertRaises(FileExistsError):
                oracle.main(["--input", str(root), "--output", str(output)])


if __name__ == "__main__":
    unittest.main()
