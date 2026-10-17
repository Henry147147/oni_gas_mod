# Native CO2 particles across colony saves

In-flight breath is material inventory. Depositing a parcel before saving would
change the simulation; losing it on reload would remove mass and energy. The
paired colony save therefore includes the original CO2 pool's outstanding
parcels alongside the completed fluid image and the original native world.

The adapter observes the five-argument `CO2Manager.SpawnCO2` factory after the
original game creates a particle. A particle whose origin belongs to the active
fluid region receives a mod-owned incarnation ID. This ID describes one parcel,
not a Unity component or native save object. Reusing a pooled component retires
its previous incarnation, including reuse outside the region. Capturing a save
also removes entries that have left the manager's active list.

During ordinary paused save admission, the adapter captures each remaining
parcel's exact binary32 position, velocity, mass, temperature, remaining lifetime,
animation rotation and flip. It verifies that these facts remain unchanged
through ordinary serialization. A particle with a `SaveLoadRoot` is rejected:
the current adapter must never serialize inventory that the game already owns
through a native save root.

`MACO2P01` version 1 contains a 24-byte header, at most 4096 canonical 64-byte
records, and a 64-byte ASCII SHA-256 checksum. Records have strictly increasing
positive IDs and zero reserved fields. Admission rejects invalid lengths,
checksums, duplicate identities, nonfinite fields, invalid mass/temperature and
coordinates outside the saved world. Positive binary32 subnormal mass is retained
without a cutoff. Expired but not yet deposited lifetime is retained as well.

Nonempty particle data uses version 3 of the `MAONID01` inner domain section.
It follows a separately length-delimited physical image, which may itself contain
durable native actors. Empty particle data retains the exact version 1 or 2
framing. Old formats remain readable. Native physical admission and the outer
paired-save checksum still apply before the original native loader runs.

On reload, positive simulation ticks stay paused until the normal CO2 manager
exists. The adapter rejects an already occupied owned pool, creates saved
particles through the original factory, restores their fields, and compares the
complete particle image byte for byte before releasing the pause. The game then
owns normal movement, deposition and retirement. Deposits enter the same native
fluid transfer journal as other material inputs; restore does not deposit mass
or consume an artificial timestep.

`--verify-transient-particles` on either Python player runner, or
`-VerifyTransientParticles` on the Windows runner, selects the development
control. A gas source also needs `--save-colony` / `-SaveColony`; the load uses
`native-load-domain` / `-NativeDomain load`. The source creates one original
0.03125 kg, 333.25 K CO2 parcel while paused immediately before saving. The load
requires exact restore, the ordinary frame-zero autosave, one completed deposition
receipt and removal from the original active pool. Full continuation uses the
accepted input journals rather than an invented particle trajectory.

The frozen `70f9cb1` real Linux source passes at frame 22, with 17 original
positive breathing callbacks and 17 native body-heat transfers totaling
336.74926264211535 J. It saves two in-flight parcels, including one produced by
the real breather. The ordinary save confirms the active particles have no
native save root. Particle-image SHA-256:
`d37c1ae7df99c43d7ca4f7c63f22ead9de0f5830ae0d7fcad8713bafa3841954`.
Source colony-file SHA-256:
`ae1f85a567ccfe9bfa79f750b1f94276ee2261396b98c212ada827e3d81ad487`.

The subsequent Linux process loads that source twice through the normal menu
lifecycle. Both sessions restore both parcels exactly and preserve the same
image in their frame-zero autosaves. Each completes 120 individually exact
input-replayed fluid frames, 120 represented native body-heat transfers and one
original deposition of the 0.03125 kg test parcel. The two sessions deliver 14
and 12 original breathing callbacks, respectively. Independent binary reviews
pass all 240 accepted-input journals. The source file remains unchanged; visual
inspection confirms the first completed PNG shows the actual duplicant in the
mixture room. Complete load-result SHA-256:
`a380a9334e4b552d4723a6e14c8205ec6383d414341f2b1a3864d34990c244dc`.

The format/parser cohort passes 165 declaration scenarios on both installed
players. WindowsPlayer and cross-player controls are running. This remains the
bounded sealed-region adapter. Whole-colony particles, conserved metabolism,
bubble/exhaust persistence and complete material support remain open.

The later [binary32 wire conversion](native-float-wire.md) preserves tiny parcel
inputs even when the managed caller enables DAZ/FTZ. The actual smallest-float
deposition control is separate from this ordinary-mass persistence result.
