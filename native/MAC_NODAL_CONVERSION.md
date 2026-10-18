# Conservative one-dimensional nodal conversion

Selector 5, `swept_nodal_1d`, repairs the interior curved-deformation point-velocity
oracle without changing its geometry, source inventories, canonical momentum or
128-epsilon allowance. It is an opt-in standalone component. It does not pass the
original strong-flow gate, support general two-dimensional motion, or enable colony
activation. Defaults remain 1 / 1 / 2 / 2.

The [existing diagnosis](MAC_STRONG_FLOW.md#curved-deformation-pressure-and-remap-convention)
showed that pressure moves material faces using point velocities, while swept
momentum reconstruction treats them as dual-volume means. Replacing source J with
an alternate mean loses the original momentum/kinetic account. This conversion
uses equal/opposite pair impulses and carries the associated signed energy work.

## Geometry and momentum

For a source material cell of mass m, fixed width h, moved width L, and its two
point velocities ul/ur, transfer the impulse

```text
Q = m (ur - ul) (1 - h²/L²) / 8
J_left += Q; J_right -= Q.
```

The implementation evaluates the geometric factor as
`(dV / V_source) (1 + V_grid / V_source) / 8`, keeping dV in full-range arithmetic.
This preserves tiny deformations and avoids subtracting nearly equal squared
widths. All requests use the incoming canonical velocities; each face gathers its
signed corrections before one canonical storage operation. No impulse is exchanged
with a fixed half dual cell. Zero stretch gives exactly zero conversion.

For uniform density and `u(x) = a + bx`, write moved faces as `xf = f h + qf`.
The dual centroid differs from xf by `(q_(f-1) - 2qf + q_(f+1))/4`. The pair
impulse can also be written `Q = rho b (L² - h²)/8`. Its divergence is precisely
the difference between the original point momentum and the required interior dual
mean. Thus momentum is conserved by the conversion and the swept remap returns
the fixed-grid affine point velocity, including curved q. This is a discrete 1-D
derivation; it does not establish a general 2-D conversion.

## Kinetic and internal work

For each face's gathered impulse dJ, keep the original canonical J and K in the
endpoint account. The virtual source conversion supplies signed work

```text
G = -(J/I) dJ - dJ²/(2I),
K_converted - K_original + G = 0.
```

Full-range arithmetic evaluates the linear and quadratic terms before summation.
Source work is gathered into the adjacent material cells by their original half
masses. It then travels with exactly the same actual primal mass flux and retained
mass as the material. Converted J and its nonnegative K use the compatible dual
fluxes. This keeps conversion work distinct from nonnegative mixing/wall heat and
the later signed remap work. The original U/J stay the immutable account source.

The completed candidate must satisfy the existing species/passive, momentum/wall,
U+K, EOS and entropy conditions. The existing common correction-fraction search
scales both source conversion and swept corrections; even fraction zero must pass.
No energy or inventory is renormalized. Primary-U rounding remains explicit and
unpersisted, as described in [range arithmetic](MAC_RANGE.md).

Conservative nodal remap and kinetic/internal reconciliation are also treated by
[Loubere and Shashkov, subcell remapping](https://www.math.ntnu.no/conservation/2004/058.html)
and [Kucharik et al., positivity-preserving staggered remapping](https://www.osti.gov/servlets/purl/1771097).
The geometry factor and material-work transfer above are this implementation's.

## Evidence and limits

`ma_mac_nodal_conversion_tests` and `review_mac_nodal_conversion.py` cover sixteen
original affine/curved controls: both axes, both sweep signs and velocity scales
1 and 2^-1200. Every actual point reading at faces 4–6 meets the original
128-epsilon oracle. The reader independently reconstructs pair gathering, kinetic
work, material-work advection, each constituent/passive account, endpoint U+K and
EOS entropy with fractions and 100-digit thermodynamics. Six corrupted captures
must fail. A further control verifies persisted selector 5, exact same-platform
checkpoint continuation, zero-stretch identity with selector 4 and atomic rejection
of simultaneous x/y motion.

The original diagnostic remains unchanged by default and still reports its failed
selector-4 point criterion. `--nodal-conversion` runs selector 5 on the same sixteen
diagnostic inputs. Its alternate dual-mean source is then a negative control: it
already contains the correction and must not be treated as a nodal source.

A bounded N64 separation probe with the unchanged scientific inputs and selectors
2 / 5 / 3 / 2 gives density/pressure/velocity L1 about 0.08597 / 0.07989 / 0.13251.
The same build at 2 / 4 / 3 / 2 gives 0.05540 / 0.05052 / 0.18507. Velocity improves;
density and pressure worsen. This does not pass the strong-flow gate. An attempted
full original profile sequence stopped at the former coupled-compression
restriction on range-projected incoming velocities. The range extension in
[coupled pressure](MAC_PRESSURE.md#coupled-compressive-pressure-work-mode-3) removes
that restriction while adding an actual cell-work check. A new automatic-PC probe
finishes Sod N64, then reaches selector 5's separate 2-D-motion rejection at N128.
An existing Jacobi-PC setting preserves the planar motion through Sod N256, but
collision N128 fails pressure refinement (about 0.947 versus 0.85). These are
diagnostic opt-in results; no production selector or bound was changed.

Captures and the bounded probe build description are under the ignored
`local/nodal-conversion/`. They are component evidence, not game, actual Windows OS
or performance acceptance.

```sh
cmake --build build-production --target ma_mac_nodal_conversion_tests -j 8
build-production/ma_mac_nodal_conversion_tests > nodal.stdout
python3 native/tests/review_mac_nodal_conversion.py nodal.stdout
```
