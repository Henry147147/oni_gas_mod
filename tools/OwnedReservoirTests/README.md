# Owned reservoir protocol checks

Compile production reservoir/actor protocol sources against a locally licensed
ONI installation and run the host checks:

```powershell
& C:/Users/henry/.dotnet/sdk10/dotnet.exe run --project tools/OwnedReservoirTests -c Release -p:OniGameDir=D:/SteamLibrary/steamapps/common/OxygenNotIncluded -- D:/SteamLibrary/steamapps/common/OxygenNotIncluded
```

The executable requires .NET 10 and resolves game references from the supplied
installation. No game assembly, native simulator or engine UI is redistributed
or launched. It checks the actual method declarations and prefix parameter
contracts. Synthetic native memory is deeply copied using the production owned
snapshot implementation; controlled managed hosts exercise the production actor
coordinator wrapper.

Store source checks use the actual managed `Pickupable.storage` property and
verify the production prefix's exact `GameObject.GetComponent<Pickupable>`
wiring. All five licensed transfer overloads reject either owned endpoint at
entry. These checks do not execute Unity item transforms or native storage
bodies; that side-effect proof belongs to the game fixture.

Only presentation registration/session notification is replaced by the small
`PresentationTestBoundary.cs` host boundary. Native-empty instance binding,
Unity lifecycle, meter animation, storage events, native descriptions and actual
Harmony installation still require the disposable in-game acceptance. Shipped
Harmony uses BinaryFormatter for patch-table serialization; .NET 10 cannot run
that Mono-specific path, so this suite does not report a simulated hook install.

See [the bridge contract](../../docs/owned-reservoir-bridge.md) for activation,
independent acknowledgements, orphan recovery and save-blocking limits.

The optional idle save eligibility lease pins the exact acknowledged Core
publication and two actor/native owner identities. It preserves empty unbound
allocations and rejects nonempty world cells, unbound mass or metadata,
callbacks/outbox, pending/unacknowledged work and recovery obligations. Normal
Core/actor writes and bind/unbind fail while held. Native callbacks and Unity
getters run outside Core locks. Detachment and session end abort the lease
before coordinator disposal and release its Core reservation in finally.

Actual native eligibility repeats exact captured component and prefab
identities, duplicate-role checks, lifetime/spawn, isolated gas ports, native
packet emptiness, construction, capacity and world/anchor checks. Component
Enabled flags are immutable diagnostics; disabled Mono updates alone do not
invalidate an otherwise healthy native component. Native instance ID zero is
valid. The focused host tests cover reentry that retires the second validator,
ordinary-unbind reentry, successor-safe disposal, wrong threads, exact
acknowledged allocation identity, observation changes and caller-owned copy
lifetime. IL checks verify actual native wiring without executing Unity.

`SaveCoreCheckpoint` returns only exact pinned Core bytes. The lease introduces
no persistent owner token or borrowed native pointer, and does not retain an
opaque companion's native resources independently. `SaveLoader.Save`, native
Storage serialization, delayed saves and save-first quit stay blocked. This is
eligibility/reservation only: no marker component, native save/load hook,
serialization permit, file write, ordinary-save integration or crash recovery.
