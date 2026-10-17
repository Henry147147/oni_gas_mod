# Verifying the actual gas GPU textures

Every new native-domain completion now reads the shader-bound gas colour,
density-opacity and hazard textures for all six owned cells. Their bytes must
match the completed mixture samples. The bound textures, physical frame and
presentation/texture publication must agree. Worker-write counters alone do
not establish a successful GPU upload.

GPU page coordinates are calibrated independently of those gas samples. The
control reads the native phase-and-mass texture for the 5-by-4 outline around
the sealed room, including its unobtanium walls and six fluid cells. It compares
the complete signature against the current game grid using both direct and
vertically reflected coordinates. Exactly one orientation must match. A
reflected rectangle also reverses row order before comparison. Missing,
contradictory or ambiguous controls reject.

The selected page-texture convention then applies to phase and gas readbacks.
Externally supplied liquid textures keep their separate existing readback path.
The report retains both raw orientation controls, their expected native
signature, selected coordinates and all gas texels. The native upload,
interpolation and world shaders remain unchanged.

This responds to a real measurement discrepancy in the earlier Linux physical
inspection: the visibly blue water room's uncalibrated phase readback instead
reported solid rock. Its gas colour/opacity readbacks were zero. The matched
controls now confirm that those reads addressed the wrong page-texture rows.
The native gas upload is correct; the readback-coordinate defect is repaired.
The private copy-upload prototype was never used in an accepted game and is
excluded from the adapter.

The portable reviewer requires the new `GpuGasTextures` extension when present,
including the independent orientation control and exact colour/alpha matches.
Older frozen reports retain their original CPU/publication scope; this does not
retroactively certify their GPU bytes. All 126 snapshot/boundary groups pass per
platform and 217 Python checks pass with four optional/platform skips. Both
adapters compile.

The actual Linux `gpu-page-orientation-workflow-v1` passes gas and water with
frozen `a91217b` and compatibility native build v11. Each Unity process loads
twice, observes shutdown and the returned menu, and advances native session 1
to 2. All four sessions pass both native startup batches, the paused frame-zero
autosave with complete body/header readback, 120 exact physical frames, restored
pressure/breathing readers and the calibrated GPU check. All four independently
select reflected page rows from the complete native outline. Their externally
supplied liquid texture readback retains its original coordinates.

All six gas colours exactly match the completed mixture samples. Density
opacity is 150 or 151 in alpha, and hazard alpha is zero. These are visual bytes,
separate from the physical pressure near 100.5 kPa. Both gas sessions retain
physical hash
`10c95fb39894b455343909a486092fa6909a4582600eca25f72ca681c98bb75e`.
All six water phase pixels are `[0,255,0,140]`, and liquid colour is
`[14,101,130,255]`. All water gas channels are zero. Both water sessions retain
physical hash
`46f8994e67552ca0146adc2980320995eef7f2ee1c01cc9ff33482a98b267d67`.
Both match the preceding physical controls exactly, with original input saves
unchanged and private displays retired.

Actual second-session hover and selected details retain the exact completed
physical object and the same publication as the checked textures. Original
Linux images show the mixed-air readings and visibly filled blue water with
220 MPa mechanical pressure, 100% liquid volume and no gas phase. The scrolled
water row keeps gas partial pressure not applicable. The preceding gas
inspection also retains the helium trace's positive mass and partial pressure.

Private calibrated GPU/image review SHA-256:
`f202eb82abfce4a13545691a59ea852b7b6019a33cf4e461e2c21a0992ed60f7`.
The raw gas and water results are
`0ca8a0d058bf1b51f6a1978fc1e94ffa49730ed680bef917fb3823cda338abcc`
and `a8ab16e30dada843d1a4123cb84d9e0736462f8cf072733361c228be4745948d`.
This accepts the bounded Linux appearance/readback and lifecycle controls;
actual Windows execution of the new calibration, arbitrary world ownership,
free surfaces, other liquids and sustained performance remain open.
