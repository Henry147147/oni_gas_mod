# Managed coupled-world boundary controls

Build `ma_coupled_api_tests` to produce the explicit coupled shared library, then
run this .NET 10 host against its exact absolute path:

```text
dotnet run --project tools/CoupledWorldTests -c Release -- <absolute coupled library path>
```

It pins the named coupled library, prints its SHA256 and runs thirteen groups using
the .NET Standard 2.1 Core binding. Controls cover model/layout agreement,
conserved/owned initialization, completed gas/liquid readings, 2D evolution and
trace conservation, whole-call rollback, independent lifetimes, concurrent
inspection and unsupported-input rejection. The two added groups check sparse
signed disease identities, tiny counts, passive radiation, actual metadata
arrival, immutable input/read ownership and metadata rollback. Five colony
coordinator groups add two-world geometry/catalogue admission, identical frame
clocks, a shared attempt budget, rollback after an earlier world completes,
explicit pure-liquid mechanical pressure, immutable frame reads and disposal.
They use original synthetic import blobs. No ONI process or licensed game
assemblies are used. Physical U and momentum remain in the persistent native
owners; the scalar total-energy Core projection is read-only and is never used
to reinitialize physics.

The [native boundary documentation](../../native/COUPLED_ABI.md) records matching
Windows/Linux evidence, preserved first build/driver failures and scope limits.
This is a binding to the existing closed reference; it does not select the
production runtime or satisfy game, vacuum, save or performance acceptance.
