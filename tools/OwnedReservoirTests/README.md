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
