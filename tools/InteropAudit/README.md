# ONI native boundary audit

This original .NET 10 tool reads a licensed managed PE assembly using `System.Reflection.Metadata`. It does not load the assembly into a runtime, resolve game dependencies, or execute game code.

```sh
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll > /tmp/oni-native-audit.json
```

Run separately on `Assembly-CSharp-firstpass.dll` and relevant installed mod assemblies. JSON includes the assembly SHA256, P/Invoke declarations, direct call and function-pointer acquisition sites, assignments to Grid fields, possible indirect pointer writers, and unresolved/indirect dispatch facts. Overloads share a readable method label; the IL offset identifies the instruction within the originating method. The first 100 unresolved token references are sampled with an exact total count. Generic `TypeSpec` parent names remain unresolved.

Pointer candidates indicate a method both reads a raw Grid pointer and performs an indirect store. Review the target of the store before calling it a Grid writer: out parameters and unrelated arrays produce false positives. Aliases passed through other methods can produce false negatives. Reflection/delegate summaries and unresolved names are explicitly reported; absence of direct calls is not proof of total interception. Native writes and other assemblies require separate audits.

Validated against licensed ONI build 744825: 68,387 method bodies, 16 native imports, 70 direct native call sites, no `calli`, one raw-pointer/store candidate (`Grid.InitializeCells`, confirmed to write build masks instead of native cell arrays). The main-game physics pointers are assigned by `Sim.Start`, `Game.StepTheSim`, shutdown, and world-generation publication. These observations are specific to the inspected binary hash, not a compatibility guarantee after game updates.

To inspect additional direct managed call sites, supply exact qualified method labels:

```sh
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll --calls-to Sim.Load,Sim.LoadWorld,Sim.Start,SimMessages.CreateSimElementsTable
```

`selectedCalls` includes direct calls and function-pointer acquisitions. This keeps the same unresolved/indirect-dispatch limitations as the native-import audit. Names are labels rather than full overload signatures; inspect method declarations where overload distinction matters.
