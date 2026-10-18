# Managed coupled-world boundary checks

This .NET 10 host tests `CoupledSimulationWorld`, the .NET Standard 2.1 Core
binding to the coupled gas/liquid `FluidWorld` C ABI. It runs against an
explicit `mixed_atmospheres_coupled` library, using original synthetic inputs
and the linked six-cell presentation scenario. No ONI process, Unity or licensed
assembly is involved.

```sh
cmake --build build --target ma_coupled_api_tests      # also builds the coupled library
dotnet run --project tools/CoupledWorldTests -c Release -- "$PWD/build/libmixed_atmospheres_coupled.so"
```

Both targets are excluded from the default build. On Windows, pass the actual
path of `mixed_atmospheres_coupled.dll`. The host pins the library, prints its
SHA256 and one `PASS` per group, and exits nonzero on any failure.

The groups cover:

- **World.** Model and layout agreement, conserved owned initialization and
  readings from one completed state. Also a 2D gas/liquid advance with traces,
  whole-call rollback on a budget failure, snapshots and forks that outlive
  disposal, and rejection of vacuum, unknown species, overfill and bad
  configuration.
- **Metadata.** Sparse signed disease identities, tiny counts and passive
  radiation through transport, with immutable inputs and metadata rollback.
- **Colony coordination.** Two-world admission, identical frame clocks, a
  shared attempt budget, rollback after one world completes, pure-liquid
  mechanical pressure and immutable frame reads.
- **Presentation.** Exact U/T/p and constituent reads, hazardous traces,
  colour-only recapture and paired rollback. In the six-cell scene, four bulk
  gases and two traces must evolve, the chlorine warning must stay, and the
  composition must change.
- **Mutations.** Conserved mutation preparation for worlds and colony frames;
  invalid mutations reject.

Physical U and momentum stay in the native owners. The scalar total-energy
projection is read-only.

This binds the closed-domain reference only. The production colony solver is
`MacClosedWorld` (`native/src/mac_*.cpp`). The checks do not cover game, vacuum,
save or performance acceptance. The ABI and platform results are in
[the coupled boundary documentation](../../native/COUPLED_ABI.md).
