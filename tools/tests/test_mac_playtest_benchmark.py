"""Saved-metric validation only; these fixtures do not execute a solver/game."""
import copy
import unittest
from tools.run_mac_playtest_benchmark import validate_metrics

class BenchmarkMetricsTests(unittest.TestCase):
    def fixture(self):
        return dict(schema=2,cells=100000,samples=3,profile='equilibrium',scene='uniform',
            simulated_seconds_per_sample=.1,whole_colony_verified=False,setup_ms=100,cold_ms=20,
            maximum_ms=20,p50_ms=18,p95_ms=20,wall_ms=[20,16,18],substeps=3,retries=0,
            pressure_iterations=0,exact_equilibrium_intervals=3,relative_mass_drift_including_trace=0,
            relative_energy_drift=0,reference_comparison_performed=False,reference_checkpoint_equal=None,
            maximum_relative_density_difference=None,maximum_relative_pressure_difference=None,
            maximum_temperature_difference_k=None,maximum_pressure_difference_over_disturbance=None,
            maximum_relative_oxygen_partial_pressure_difference=None,maximum_relative_constituent_difference_including_trace=None,
            sampling='repeated_source',minimum_active_cells=0,wave_amplitude_pa=0,checkpoint_captured=True,checkpoint_capture_ms=1,
            maximum_timestep_s=.01,material_reconstruction=1,momentum_reconstruction=1,compression_viscosity=2,shock_heat=2,
            **{'100k_25ms_this_case_this_host':False,'250k_62_5ms_solver_only_this_host':False})

    def review(self,data): validate_metrics(data,400,250,3,'equilibrium','uniform',True)

    def test_valid_and_false_target(self):
        data=self.fixture(); self.review(data)
        data.update(cold_ms=30,maximum_ms=30,p95_ms=30,wall_ms=[30,16,18])
        data['100k_25ms_this_case_this_host']=False; self.review(data)

    def test_equilibrium_cannot_certify_active_cells(self):
        data=self.fixture();data['100k_25ms_this_case_this_host']=True
        with self.assertRaises(ValueError): self.review(data)
        data.update(scene='wave',wave_amplitude_pa=20,minimum_active_cells=100000)
        validate_metrics(data,400,250,3,'equilibrium','wave',True)
        data['minimum_active_cells']=99999
        with self.assertRaises(ValueError): validate_metrics(data,400,250,3,'equilibrium','wave',True)

    def test_250k_requires_evolving_continuation_and_measured_p95(self):
        data=self.fixture();data.update(cells=250000,scene='wave',wave_amplitude_pa=20,minimum_active_cells=250000,sampling='continuation')
        data['250k_62_5ms_solver_only_this_host']=True
        validate_metrics(data,1000,250,3,'equilibrium','wave',True,True)
        for key,value in (('sampling','repeated_source'),('minimum_active_cells',249999),('p95_ms',63)):
            bad=copy.deepcopy(data);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_metrics(bad,1000,250,3,'equilibrium','wave',True,True)

    def test_wave_comparison_requires_disturbance_and_trace_readings(self):
        data=self.fixture();data.update(scene='wave',wave_amplitude_pa=20,minimum_active_cells=100000,
            reference_comparison_performed=True,reference_checkpoint_equal=True,maximum_relative_density_difference=0,
            maximum_relative_pressure_difference=0,maximum_temperature_difference_k=0,maximum_pressure_difference_over_disturbance=0,
            maximum_relative_oxygen_partial_pressure_difference=0,maximum_relative_constituent_difference_including_trace=0)
        data['100k_25ms_this_case_this_host']=True
        validate_metrics(data,400,250,3,'equilibrium','wave',False)
        for key in ('maximum_pressure_difference_over_disturbance','maximum_relative_constituent_difference_including_trace',
                'maximum_relative_oxygen_partial_pressure_difference'):
            bad=copy.deepcopy(data);bad[key]=None
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_metrics(bad,400,250,3,'equilibrium','wave',False)

    def test_corruptions_reject(self):
        base=self.fixture()
        for key,value in (('p95_ms',18),('cold_ms',16),('maximum_ms',19),('wall_ms',[1,2]),
                ('relative_energy_drift',float('nan')),('relative_mass_drift_including_trace',-1),
                ('substeps',True),('exact_equilibrium_intervals',4),('cells',99999),
                ('reference_checkpoint_equal',True),('maximum_temperature_difference_k',0),
                ('100k_25ms_this_case_this_host',True),('250k_62_5ms_solver_only_this_host',True),
                ('whole_colony_verified',True),('minimum_active_cells',True),('minimum_active_cells',100001),
                ('maximum_pressure_difference_over_disturbance',0),('checkpoint_captured',False),('shock_heat',0)):
            with self.subTest(key=key):
                data=copy.deepcopy(base); data[key]=value
                with self.assertRaises((ValueError,KeyError,TypeError)): self.review(data)

if __name__=='__main__': unittest.main()
