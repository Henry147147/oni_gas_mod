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
reported solid rock. Its gas colour/opacity readbacks were zero. Those raw
readings cannot identify the owned cells until their coordinate convention is
established. The gas-upload defect hypothesis remains unconfirmed. The private
copy-upload prototype has not been used in an accepted game and is excluded
from the adapter.

The portable reviewer requires the new `GpuGasTextures` extension when present,
including the independent orientation control and exact colour/alpha matches.
Older frozen reports retain their original CPU/publication scope; this does not
retroactively certify their GPU bytes. All 126 snapshot/boundary groups pass per
platform and 217 Python checks pass with four optional/platform skips. Both
adapters compile. Matched actual Linux gas and water reload controls are next.
