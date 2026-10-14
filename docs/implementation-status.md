# Implementation status

This is a private development milestone. The requested overhaul is incomplete.
The first public release remains gated on complete gas **and** liquid gameplay.

## Implemented and exercised

| Area | Evidence | Limits |
| --- | --- | --- |
| Native mixture/thermodynamic reference core | 56/56 native scenarios on Windows and Linux, plus Linux ASan/UBSan | Constant properties; incomplete material coverage |
| Gas finite-volume baseline | Hydrostatic, buoyancy, diffusion, pressure, vacuum and conservation tests | Gas only; explicit acoustic performance fails target |
| Native persistence | Roundtrip, continuation, corruption and registry-reordering tests | Not connected to colony saves |
| Managed API, settings, native bootstrap and import parser | 12/12 scenarios on Linux and Windows | Game lifecycle still separate |
| Pump/separator reference processes | Enthalpy, shaft-work, entropy and trace-separation tests | No game power/storage/heat-sink transactions |
| Implicit heat exchange | Analytic pair solution, stiff contacts, energy/entropy and insulation checks | Not yet connected to native solid/building transfers |
| Native v15 fluid importer | Extracts fluid species, mass, temperature, radiation and disease before native Load | Guard/backwall fluids and unknown elements reject; no automatic existing-save conversion |
| Sparse pressure operator | Analytic and manufactured solutions, wall isolation, singularity checks | Not yet integrated into a fluid timestep |
| Linear acoustic pressure/velocity step | Large-timestep pair mode, hydrostatic balance, incompressible projection, work/dissipation tests | Frozen coefficients; no nonlinear thermodynamic or advective coupling |
| Nonlinear material pressure work | Seven scenarios check volume/temperature/energy/entropy coupling, acoustic limit, liquid force transmission, gravity, reversibility and rejection | Moving material volumes only; no Eulerian remap, shocks, vacuum or phase changes |
| Gas/liquid bulk remap | Constituent and trace preservation, bounded liquid fractions, momentum/energy and velocity-mixing heat tests | Prescribed face volumes; pressure/free surfaces/phase changes are not yet coupled |
| Windows compilation | MSVC native build; 56 scenarios passed, zero compiler warnings | Full colony behavior remains untested |
| Actual ONI references | Compiled against licensed build 744825 | Two Unity dependency-version warnings |
| ONI probe | Actual Windows and Linux Unity reports passed all six checks | Scoped loading/interception only; no colony simulation replacement |

Linux Release, Linux ASan/UBSan and Windows MSVC passed all 56 native scenarios,
including the seven nonlinear material-pressure checks. The Windows native56
build is stored separately from the frozen game-tested packages. The twelve
managed/native/settings/parser tests also passed
without placing the native library in the application or working folder.
The frozen `MixedAtmospheresProbe-windows-json49` and corresponding Linux
package passed inside their actual Unity runtimes against the 49-scenario
native builds. Windows DLL SHA256:
`1677c0fce99e096a44cfa1ebdf86f790a2999cbbbf38e13f9389f76c49ff0c1a`.
The probe verifies the inspected managed assembly hash and layouts before
installing any patches.

## Current integration evidence

The corrected Windows/WSL address, 192.168.1.202, is reachable. The licensed
installation was downloaded through the user's Steam client and copied to the
requested SMB share. The user confirmed they had read and agreed to ONI's
first-launch license. It was accepted through Steam, and ONI reached its main
menu. Steam remains open on the user's desktop.

No colony was loaded. Original `mods.json` and `kplayerprefs.yaml` were restored
byte-for-byte, and no ONI process remains from the test. Evidence is stored
outside the repository under
`/mnt/data/share/oni/mod-development/probe-evidence`.

The expanded verified Unity report records 55 redirected native-message methods,
successful native mixture roundtrip, frame/snapshot/direct-message interception,
save interception, and a settings serialization roundtrip. It reports the expected Cell16, Frame28 and Snapshot496
byte layouts. `AuthoritativeColonySimulationReady` remains false.

The strengthened probe redirects direct native message call sites in `Sim`,
`SimMessages`, `DebugHandler` and `SubworldZoneRenderData` while its own scope
is active. The latter two callers were found by the assembly-wide metadata
audit and verified in the second runtime pass. It also exercises
the managed save entry point into an in-memory buffer. Unsupported scoped
messages/batches fail explicitly. Conduit services, complete callbacks,
third-party call sites, and real colony save/load remain outside its coverage.
The probe checks patch installation before calling methods that would otherwise
reach ONI's native singleton. A capture-only scope exports private element and
disease messages for the isolated native harness, without sending those
messages to the game's simulator. Current ownership research and its explicit
remaining proof obligations are in [the native contract](interop-contract.md).

## Isolated native ownership experiments

The real Windows SimDLL ran in a separate process with a disposable 64×64
world and the actual exported element/disease tables. Native flow and solid
heat conduction worked. ModifyCell followed by snapshot requests, without a
frame, did **not** clear native fluids; that activation path is rejected.

Two alternatives passed narrow tests. Rebuilding a fresh native world with
fluids captured separately preserved nonfluid cell data and kept native fluid
cells at zero through twelve frames. The preferred path filters the native
v15 load blob before sending it to the original loader. It captured 2 kg of
oxygen and 100 kg of water, preserving temperature, species and coordinates.
The resulting native world contained no fluids before any frame and stayed
fluid-free for twelve frames while solid heat conduction continued. Its
nonfluid cells exactly matched an unmodified native-load control.

A positive-control ice cell melted with normal definitions; suppressing
fluid-producing element definitions kept it solid. This covers one transition
branch, not every source. The native message queue is asynchronous, so the
harness polls snapshots until a submitted frame is acknowledged.

Evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-v4`, `-v5` and
`-v7`. The shared import parser is in `NativeWorldBlob.cs`; the native process
harness is under `tools/NativeShadowProbe`. No test opened a colony or
certifies live Grid projection, all callbacks, complete native source
suppression, liquid transport or release readiness.

The matching Linux base depot was downloaded through the authenticated Windows
Steam client into separate depot storage, then copied to
`/mnt/data/share/oni/OxygenNotIncluded-linux`. Its inspected native library has
SHA256 `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d`.
All five positive isolated-native scenarios also passed on Linux, using the
Windows-exported element/disease tables. The negative sanitation control
reproduced the same two fluid cells and 102 kg before any frame. Initial native
saves were byte-identical between platforms. Linux evidence and the six-result
suite summary are in `shadow-evidence-linux-v2`.

Actual Linux Unity subsequently loaded the mod and passed the same six scoped
checks under Xvfb/Mesa. Its first run exposed a JSON compatibility bug: the
depot omits the Mono machine configuration required by DataContract JSON.
Reports now use the game's existing Newtonsoft assembly by reference only;
standalone settings use a strict fixed-schema codec with locale and malformed
input checks. Evidence is in `probe-evidence/linux-unity-json49`. Linux exited
before the main menu with no local Steam session, so it did not export its own
element tables. Windows main-menu validation of the same fix also passed;
the user's settings were restored exactly and the temporary mod was archived
outside the mods tree.

## Measured performance

The reference benchmark uses 400 × 250 cells, advances 0.001 simulated seconds,
and reports its own timing and conservation errors. An initial local Release
run on the Ryzen 9 5900X took 425.3508 ms and two acoustic substeps. This is not
close to the proposed 25 ms budget per 0.1 simulated seconds for 100,000 active
cells. The benchmark is deliberately retained as a failing baseline; no
real-time claim should be made from the small test scenarios.

## Remaining release gates, in dependency order

1. **Authoritative integration.** Audit and redirect every native mutation,
   snapshot, callback, save, and conduit service. Prove lifetime/ownership of
   native arrays and texture buffers, and prove that a disposable new colony
   has exactly one owner for every simulated quantity. Initial scoped patch
   interception is insufficient.
2. **Production fluid solver.** Implement and validate the efficient all-Mach
   coupling of pressure, conservative transport and thermodynamics, plus
   liquid transport, miscibility/immiscibility, free
   surfaces, displacement, and grid-coupled phase transitions. Preserve mass,
   momentum, energy and trace constituents across all paths.
3. **Complete thermodynamic catalogue.** Cover every supported game material,
   including documented fictional surrogates. Add valid-range property tables,
   entropy/free-energy constraints, and all machine/biological reaction audits.
4. **All fluid users.** Replace pumps, filters, vents, pipes, reservoirs, suits,
   rockets, consumers, emitters, generators, plants, critters, geysers, waste,
   and fluid-related disease/radiation/thermal behavior. No silent vanilla
   conversion path may remain.
5. **Playable colony.** Register nitrogen and its relevant phases, starter
   supplies, ventilation/scrubbing, sensors, overlays, research and recipes.
   Apply configurable breathing rules to duplicants and suitable equipment.
6. **Colony persistence and compatibility.** Save all mixture state; reject
   unsupported versions and incompatible mods before load. Validate restart,
   autosave, multi-asteroid transfer, and changed display settings.
7. **Release validation.** Run representative long colonies for base game and
   Spaced Out! on Windows and Linux; measure CPU time, allocations, memory,
   conservation and stability. Review gameplay progression and all release
   gates before creating a Workshop package.

There is no release-packaging option. `tools/package_probe.py` produces only an
explicit integration probe and writes `release_ready: false` in its manifest.
The Steam Community draft describes the intended project and its current
development status; it is not a release announcement.
