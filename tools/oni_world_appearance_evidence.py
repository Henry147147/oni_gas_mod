"""Reconstruct bounded world appearance from saved material and EOS operands.

This module imports no managed code and does not run the fluid solver. GPU
binding, orientation and publication checks belong to the native-domain gate.
"""
from __future__ import annotations

import math


def _finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def _linear(value):
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def _encode(value):
    value = min(1, max(0, value))
    return 12.92 * value if value <= .0031308 else 1.055 * value ** (1 / 2.4) - .055


def _byte(value):
    return math.floor(min(1, max(0, value)) * 255 + .5)


def _pattern(material_id, x, y):
    hashed = 2166136261
    encoded = material_id.encode('utf-16-le')
    for offset in range(0, len(encoded), 2):
        hashed = ((hashed ^ int.from_bytes(encoded[offset:offset+2], 'little')) * 16777619) & 0xffffffff
    phase_x = (hashed & 1023) * (2 * math.pi / 1024)
    phase_y = ((hashed >> 10) & 1023) * (2 * math.pi / 1024)
    return (math.cos((x % 4) * math.pi / 2 * (1 + ((hashed >> 20) & 1)) + phase_x)
            * math.cos((y % 4) * math.pi / 2 * (1 + ((hashed >> 21) & 1)) + phase_y))


def world_appearance_passed(report: dict) -> bool:
    """Require exact byte reconstruction of every published mixed/dry pixel."""
    try:
        inputs = report['RenderingInputs']
        settings = inputs['WorldAppearance']
        if (settings['Model'] != 'actual-phase-volume-linear-rgb-v1'
                or settings['LiquidInterfaceGeometry'] is not False
                or type(settings['PatternEnabled']) is not bool
                or not _finite(settings['PatternContrast']) or not 0 <= settings['PatternContrast'] <= .25):
            return False
        grid_width, grid_height = settings['GridWidth'], settings['GridHeight']
        if any(type(v) is not int or v <= 0 for v in (grid_width, grid_height)):
            return False
        opacity = settings['Opacity']
        low, high, minimum = (opacity[k] for k in ('Minimum', 'Maximum', 'MinimumVisibility'))
        if any(not _finite(v) for v in (low, high, minimum)) or high <= low or not 0 <= minimum <= 1:
            return False
        styles = {}
        for item in settings['Styles']:
            identity, rgb = item['MaterialId'], item['Srgb']
            if (not isinstance(identity, str) or not identity or identity in styles or len(rgb) != 3
                    or any(not _finite(v) or not 0 <= v <= 1 for v in rgb)):
                return False
            styles[identity] = [_linear(v) for v in rgb]
        rows = inputs['Rows']
        if len(rows) != report['NativeDomain']['Cells'] or not rows:
            return False
        for index, row in enumerate(rows):
            cell = row['Cell']
            if type(cell) is not int or not 0 <= cell < grid_width * grid_height:
                return False
            x, y = cell % grid_width, cell // grid_width
            if (x, y) != (report['X'] + index % report['Width'], report['Y'] + index // report['Width']):
                return False
            reading = row['Reading']
            volume, gas_volume, liquid_volume, gas_mass = (reading[k] for k in
                ('VolumeCubicMetres', 'GasVolumeCubicMetres', 'LiquidVolumeCubicMetres', 'GasMassKg'))
            if (not _finite(volume) or volume <= 0 or any(not _finite(v) or v < 0 for v in (gas_volume, liquid_volume, gas_mass))
                    or gas_volume > volume * (1 + 2e-10) or liquid_volume > volume * (1 + 2e-10)):
                return False
            parts, identities = reading['Constituents'], set()
            moles = []
            for part in parts:
                identity, phase = part['MaterialId'], part['Phase']
                mass, molar, partial = (part[k] for k in ('MassKg', 'MolarMassKgPerMol', 'PartialVolumeCubicMetres'))
                if (identity in identities or identity not in styles or type(phase) is not int or phase not in (1, 2)
                        or any(not _finite(v) or v <= 0 for v in (mass, molar, partial))):
                    return False
                identities.add(identity)
                if phase == 1:
                    moles.append(mass / molar)
            total_moles = math.fsum(moles)
            colour = [0., 0., 0.]
            mixed = gas_volume > 0 and liquid_volume > 0
            for part in parts:
                rgb = styles[part['MaterialId']]
                if part['Phase'] == 1:
                    weight = part['MassKg'] / part['MolarMassKgPerMol'] / total_moles
                    if settings['PatternEnabled']:
                        weight *= 1 + settings['PatternContrast'] * _pattern(part['MaterialId'], x, y)
                    if mixed:
                        weight *= gas_volume / volume
                elif mixed:
                    weight = part['PartialVolumeCubicMetres'] / volume
                else:
                    continue
                for channel in range(3):
                    colour[channel] += weight * rgb[channel]
            expected = row['ExpectedGas']
            if gas_mass > 0 and gas_volume > 0:
                rgba = [_byte(_encode(v)) for v in colour] + [255]
                density_visibility = max(minimum, min(1, max(0, (gas_mass / gas_volume * 101.3 - low) / (high - low))))
                visible = density_visibility * gas_volume / volume + liquid_volume / volume if mixed else density_visibility
                alpha = _byte(visible)
            else:
                rgba, alpha = [0, 0, 0, 0], 0
            if expected['Colour'] != rgba or expected['Pressure'] != alpha:
                return False
        return True
    except (KeyError, TypeError, ValueError, AttributeError, ZeroDivisionError, OverflowError):
        return False
