# Private load counter declaration preflight

This focused host check scans the exact licensed Windows744825
Assembly-CSharp original IL without invoking a game method, installing Harmony,
initializing Unity or calling SimDLL. The optional second argument is a privately
built real MixedAtmospheres.Oni.dll; no game-boundary stub is substituted.

The independent scanner decodes instruction and operand boundaries across every
managed body, finding exactly four one-call SIM_Initialize callers: three
SaveLoader bodies and the full nine-parameter
ProcGenGame.WorldGenSimUtil.DoSettleSim declaration. It records declaring types,
parameter identities, metadata tokens and original IL hashes. It then exercises
the helper's pure closure validator with complete, missing and duplicate lists;
the former three-caller assumption must reject.

InspectContracts validates the actual helper's complete45-method contract,
12 native callsite specifications,15 native-map keys and4 managed antecedent
keys. Every original native callsite must occur once, each target must have a
normal return, and iterator targets must resolve through their exact compiler
state-machine attributes. Repeated immutable inspection must leave all runtime
counter registries empty and Installed false. Output values have public getters
for serialized evidence.

The four additional targets are the exact `Game.OnPrefabInit`, inherited
`KMonoBehaviour.OnDestroy`, `SceneManager.Internal_SceneUnloaded(Scene)` and
`KBatchedAnimUpdater.DoGridProcessing` bodies. The host independently resolves
their declarations and confirms their inclusion and normal-return sites. The
Unity CoreModule hash is pinned as well. These checks invoke no lifecycle,
scene, animation, or native method; actual matching loader/scene completion
still requires the private Mono game run.

Use private output paths:

```powershell
dotnet build managed/MixedAtmospheres.Oni --artifacts-path local/reservoir-load-build/counters-v3-artifacts/product -p:OniGameDir=D:/SteamLibrary/steamapps/common/OxygenNotIncluded
dotnet build tools/OwnedReservoirLoadCounterTests --artifacts-path local/reservoir-load-build/counters-v3-artifacts/host
dotnet local/reservoir-load-build/counters-v3-artifacts/host/bin/OwnedReservoirLoadCounterTests/debug/OwnedReservoirLoadCounterTests.dll D:/SteamLibrary/steamapps/common/OxygenNotIncluded local/reservoir-load-build/counters-v3-artifacts/product/bin/MixedAtmospheres.Oni/debug/MixedAtmospheres.Oni.dll
```

With only the first argument it enumerates licensed initialization callers and
exits; full helper validation requires both arguments. The runtime installation,
composed transpiler IL and actual body/native-dispatch calibration still require
the separate private Mono game acceptance. There is no save permission, owned
restore, successful load claim or Linux Unity claim here.

The separate actual Windows v33 private run installs the composed hooks and
verifies complete 45-method, 15-native-key and four-antecedent maps with zero
observation failures. Ordinary native save/load and positive animation calibrate
their relevant original bodies and dispatch; zero counts remain explicit.
All three rejected epochs complete their intended real native lifecycle and
stable frontend. Its exact package/report/source evidence remains private in
`local/colony-runs/load-rejection-windows-v33`. This host tool still invokes no
game method and grants no owned restore/save permission.
