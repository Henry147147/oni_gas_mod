"""Saved-metric validation only; these fixtures do not execute a solver/game."""
import copy
import unittest
from tools.run_mac_playtest_benchmark import validate_metrics

class BenchmarkMetricsTests(unittest.TestCase):
    def fixture(self):
        return dict(schema=1,cells=100000,samples=3,profile='equilibrium',scene='uniform',
            simulated_seconds_per_sample=.1,whole_colony_verified=False,setup_ms=100,cold_ms=20,
            maximum_ms=20,p50_ms=18,p95_ms=20,wall_ms=[20,16,18],substeps=3,retries=0,
            pressure_iterations=0,exact_equilibrium_intervals=3,relative_mass_drift_including_trace=0,
            relative_energy_drift=0,reference_comparison_performed=False,reference_checkpoint_equal=None,
            maximum_relative_density_difference=None,maximum_relative_pressure_difference=None,
            maximum_temperature_difference_k=None,**{'100k_25ms_this_case_this_host':True})

    def review(self,data): validate_metrics(data,400,250,3,'equilibrium','uniform',True)

    def test_valid_and_false_target(self):
        data=self.fixture(); self.review(data)
        data.update(cold_ms=30,maximum_ms=30,p95_ms=30,wall_ms=[30,16,18])
        data['100k_25ms_this_case_this_host']=False; self.review(data)

    def test_corruptions_reject(self):
        base=self.fixture()
        for key,value in (('p95_ms',18),('cold_ms',16),('maximum_ms',19),('wall_ms',[1,2]),
                ('relative_energy_drift',float('nan')),('relative_mass_drift_including_trace',-1),
                ('substeps',True),('exact_equilibrium_intervals',4),('cells',99999),
                ('reference_checkpoint_equal',True),('maximum_temperature_difference_k',0),
                ('100k_25ms_this_case_this_host',False),('whole_colony_verified',True)):
            with self.subTest(key=key):
                data=copy.deepcopy(base); data[key]=value
                with self.assertRaises((ValueError,KeyError,TypeError)): self.review(data)

if __name__=='__main__': unittest.main()
