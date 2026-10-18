# ONI native boundary audit

This .NET 10 tool reads a licensed managed PE assembly with
`System.Reflection.Metadata`. It never loads the assembly into a runtime,
resolves game dependencies or runs game code.

```sh
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll > /tmp/oni-native-audit.json
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll \
  --calls-to Sim.Load,Sim.LoadWorld,Sim.Start,SimMessages.CreateSimElementsTable
```

Run it separately on `Assembly-CSharp.dll`, `Assembly-CSharp-firstpass.dll` and
any relevant mod assemblies. The output is game-derived, so keep it out of the
repository. The JSON records:

- the assembly's SHA256 and its P/Invoke declarations;
- direct call sites and function-pointer acquisitions;
- assignments to `Grid` fields and possible indirect pointer writers;
- unresolved and indirect dispatch, with the first 100 unresolved tokens sampled
  and an exact total.

Overloads share a readable label, and an IL offset locates each instruction.
Generic `TypeSpec` parents stay unresolved. `--calls-to` adds `selectedCalls`
for the named methods. Labels are not full signatures, so check the
declarations where overloads matter.

A pointer candidate is a method that reads a raw `Grid` pointer and also stores
indirectly. Review the store target before calling it a writer: out parameters
and unrelated arrays give false positives, and aliases passed through other
methods give false negatives. Missing direct calls do not prove complete
interception. Native writes and other assemblies need separate audits.

For build 744825, the audit finds:

- 68,387 method bodies, 16 native imports and 70 direct native call sites;
- no `calli`;
- one candidate, `Grid.InitializeCells`, which writes build masks rather than
  native cell arrays.

`Sim.Start`, `Game.StepTheSim`, shutdown and world-generation publication assign
the physics pointers. These findings hold only for the inspected binary hash, so
re-audit after a game update. See [the interop contract](../../docs/interop-contract.md).
