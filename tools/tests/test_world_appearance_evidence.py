import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from oni_world_appearance_evidence import world_appearance_passed


class WorldAppearanceEvidenceTests(unittest.TestCase):
    def fixture(self):
        # 80% gas / 20% compressed water: red and blue in linear light.
        # 250 kg / nominal 1000 kg/m3 would incorrectly give 25% liquid.
        return dict(X=119, Y=207, Width=1, NativeDomain=dict(Cells=1), RenderingInputs=dict(
            WorldAppearance=dict(Model='actual-phase-volume-linear-rgb-v1', GridWidth=256, GridHeight=512,
                PatternEnabled=False, PatternContrast=.04, LiquidInterfaceGeometry=False,
                Opacity=dict(Minimum=15, Maximum=200, MinimumVisibility=.1),
                Styles=[dict(MaterialId='oxygen', Srgb=[1, 0, 0]), dict(MaterialId='water', Srgb=[0, 0, 1])]),
            Rows=[dict(Cell=119+207*256, Reading=dict(VolumeCubicMetres=1, GasVolumeCubicMetres=.8,
                LiquidVolumeCubicMetres=.2, GasMassKg=1, Constituents=[
                    dict(MaterialId='oxygen', Phase=1, MassKg=1, MolarMassKgPerMol=.032, PartialVolumeCubicMetres=.8),
                    dict(MaterialId='water', Phase=2, MassKg=250, MolarMassKgPerMol=.018, PartialVolumeCubicMetres=.2)]),
                ExpectedGas=dict(Colour=[231, 0, 124, 255], Pressure=174, Danger=0))]))

    def test_independent_compressed_volume_pixel(self):
        source = self.fixture(); original = copy.deepcopy(source)
        self.assertTrue(world_appearance_passed(source))
        self.assertEqual(source, original)

    def test_rejects_gas_only_mass_weighted_and_nominal_volume_pixels(self):
        for colour, opacity in [([255, 0, 0, 255], 154), ([225, 0, 137, 255], 179), ([13, 0, 255, 255], 255)]:
            report = self.fixture()
            report['RenderingInputs']['Rows'][0]['ExpectedGas'].update(Colour=colour, Pressure=opacity)
            self.assertFalse(world_appearance_passed(report))

    def test_rejects_changed_operands_and_each_pixel_channel(self):
        changes = [
            lambda r: r['WorldAppearance'].update(Model='nominal-density'),
            lambda r: r['WorldAppearance'].update(LiquidInterfaceGeometry=True),
            lambda r: r['WorldAppearance'].update(GridWidth=512),
            lambda r: r['WorldAppearance'].update(PatternContrast=float('nan')),
            lambda r: r['WorldAppearance'].update(PatternEnabled=1),
            lambda r: r['WorldAppearance']['Opacity'].update(Minimum=200),
            lambda r: r['WorldAppearance']['Styles'].append(r['WorldAppearance']['Styles'][0]),
            lambda r: r['WorldAppearance']['Styles'][0].update(Srgb=[float('inf'), 0, 0]),
            lambda r: r['Rows'][0]['Reading'].update(GasMassKg=.5),
            lambda r: r['Rows'][0]['Reading'].update(GasVolumeCubicMetres=.75),
            lambda r: r['Rows'][0]['Reading']['Constituents'][1].update(PartialVolumeCubicMetres=.25),
            lambda r: r['Rows'][0]['Reading']['Constituents'][0].update(MaterialId='unknown'),
            lambda r: r['Rows'][0]['Reading']['Constituents'][0].update(Phase=3),
            lambda r: r['Rows'][0]['Reading']['Constituents'][0].update(MolarMassKgPerMol=0),
            lambda r: r['Rows'][0].update(Cell=0),
            lambda r: r['Rows'][0]['ExpectedGas'].update(Pressure=175),
        ]
        for channel in range(4):
            changes.append(lambda r, c=channel: r['Rows'][0]['ExpectedGas']['Colour'].__setitem__(c, 1))
        for change in changes:
            report = self.fixture(); change(report['RenderingInputs'])
            self.assertFalse(world_appearance_passed(report))

    def test_dry_canonical_and_gasless_pixels(self):
        report = self.fixture(); row = report['RenderingInputs']['Rows'][0]
        row['Reading'].update(GasVolumeCubicMetres=1, LiquidVolumeCubicMetres=0)
        row['Reading']['Constituents'] = [dict(MaterialId='oxygen', Phase=1, MassKg=1, MolarMassKgPerMol=.032, PartialVolumeCubicMetres=1)]
        row['ExpectedGas'].update(Colour=[255, 0, 0, 255], Pressure=119)
        self.assertTrue(world_appearance_passed(report))
        row['Reading'].update(GasMassKg=0, GasVolumeCubicMetres=0, LiquidVolumeCubicMetres=1)
        row['Reading']['Constituents'] = [dict(MaterialId='water', Phase=2, MassKg=1250, MolarMassKgPerMol=.018, PartialVolumeCubicMetres=1)]
        row['ExpectedGas'].update(Colour=[0, 0, 0, 0], Pressure=0)
        self.assertTrue(world_appearance_passed(report))

    def test_multiple_liquids_contribute_their_own_colours(self):
        report = self.fixture(); inputs = report['RenderingInputs']; row = inputs['Rows'][0]
        inputs['WorldAppearance']['Styles'].append(dict(MaterialId='oil', Srgb=[0, 1, 0]))
        row['Reading'].update(GasVolumeCubicMetres=.5, LiquidVolumeCubicMetres=.5, GasMassKg=.625)
        row['Reading']['Constituents'][0].update(MassKg=.625, PartialVolumeCubicMetres=.5)
        row['Reading']['Constituents'].append(dict(MaterialId='oil', Phase=2, MassKg=270, MolarMassKgPerMol=.15, PartialVolumeCubicMetres=.3))
        row['ExpectedGas'].update(Colour=[188, 149, 124, 255], Pressure=204)
        self.assertTrue(world_appearance_passed(report))


if __name__ == '__main__':
    unittest.main()
