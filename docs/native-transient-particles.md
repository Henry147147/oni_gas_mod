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

The Linux source also passes two WindowsPlayer reload sessions under official
Proton, each with 120 exact frames, the two restored parcels and one test-parcel
deposition. Their actual breathing callback counts are 14 and 13; their body
heat losses are 2576.449547195807 J and 2582.8033068683 J. Both independent
journal reviews pass. A separate saved-only review retains the private
controller's Linux-only cleanup-field mismatch after the game had already
passed. Raw game-result SHA-256:
`6692f1a02b1a1876fa370b84ce69e8fd1913b4e0cf65749f748b23bfbdc4f662`.
Review SHA-256:
`947f89d9fb7cf10cdb20a7798367c5d512e0fcc3523aee5f4c003689bc86d327`.

The WindowsPlayer-origin source at the same frozen revision also passes. It
saves two in-flight parcels after 18 positive original breathing callbacks.
Both local WindowsPlayer reloads and both Linux reloads restore those parcels,
preserve them in frame-zero autosaves and complete 120 individually exact
continuation frames per session. Each continuation has a separate passing
binary journal review. The local and cross-platform load-result SHA-256 values
are `b9aafc785f31d0d6943c8346fba258b4bf302545a28d0d5aa17aea4fb19dc847`
and `ed96715c3bc847cea127808725ed6b70ec7afe7c752a6683bc6dd8343937dedd`.
WindowsPlayer evidence uses official Proton on Linux; an actual Windows OS
run remains a separate gate.

The format/parser cohort passes 165 declaration scenarios on both installed
players. This remains the bounded sealed-region adapter.
Whole-colony particles, conserved metabolism,
bubble/exhaust persistence and complete material support remain open.

The later [binary32 wire conversion](native-float-wire.md) preserves tiny parcel
inputs even when the managed caller enables DAZ/FTZ. The actual smallest-float
deposition control is separate from this ordinary-mass persistence result.

The retirement witness now latches the parcel identity when it leaves the
original active pool. Subsequent native factory reuse of its Unity component
is recorded separately and cannot undo that original retirement. The combined
smallest-float in-flight save control checks exact restore, one deposit and a
positive transported CO2 inventory from an initially empty constituent.

The combined smallest-float control now passes its actual Linux source and
both Linux reload sessions at frozen `05cda0c`. Each restores the one parcel
byte for byte, deposits exactly `2^-149` kg once at frame 10 and completes
120 individually exact frames. The final CO2 inventory remains positive at
`1.4012984643248147e-45` kg, within the unchanged 128-epsilon operand bound.
Both sessions observe later native pool reuse without undoing the retirement
witness. All 240 binary journals pass independent saved-only review. Review
SHA-256: `20702088260063142b4dfc7f720dd3f8e57c9702b398db481e858bfffe024876`.
The unchanged Linux source also passes both WindowsPlayer reload sessions:
one exact restore and original frame-10 deposit, later native pool reuse, and
120 individually exact frames per session. Both end with positive CO2 mass
`1.401298464324819e-45` kg within the same operand bound. All 240 journals pass
independent saved-only review, SHA-256
`9b05dd7628d02a11787f07bb3a24fbf83e64fc1292bb253caf5391eeebd8f3da`.
The WindowsPlayer-origin tiny parcel source and all four of its local and
Linux continued sessions also pass. Across both sources, all eight reload
sessions preserve their frame-zero saves, one exact frame-10 deposit, positive
transported inventory and all 120 individually exact frames. Every session
observes later native pool reuse without losing its retirement witness. Both
source files remain unchanged. The complete 960-frame saved-only journal and
source-file review has SHA-256
`10eb0eb46344dc24e1a28bf59fe1847fb35417a7914c09803706c4a97f112af0`.
