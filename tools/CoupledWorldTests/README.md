# Managed coupled-world boundary controls

Build `ma_coupled_api_tests` to produce the explicit coupled shared library, then
run this .NET 10 host against its exact absolute path:

```text
dotnet run --project tools/CoupledWorldTests -c Release -- <absolute coupled library path>
```

It pins the named coupled library, prints its SHA256 and runs six groups using
the .NET Standard 2.1 Core binding. Controls cover model/layout agreement,
conserved/owned initialization, completed gas/liquid readings, 2D evolution and
trace conservation, whole-call rollback, independent lifetimes, concurrent
inspection and unsupported-input rejection. No ONI process or licensed game
assemblies are used.

The [native boundary documentation](../../native/COUPLED_ABI.md) records matching
Windows/Linux evidence, preserved first build/driver failures and scope limits.
This is a binding to the existing closed reference; it does not select the
production runtime or satisfy game, vacuum, save or performance acceptance.
