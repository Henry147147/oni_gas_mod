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

The format and parser pass 165 declaration scenarios against both installed
Linux and Windows assemblies. The full adapter compiles against both players;
actual source/save/reload controls are pending. This remains the bounded sealed
region adapter. It does not establish whole-colony particle persistence,
conserved duplicant metabolism, bubble/exhaust persistence or complete material
support.
