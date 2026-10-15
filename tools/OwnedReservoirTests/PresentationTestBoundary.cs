using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni;

// Host tests replace only presentation registration/session notification.
// They do not pretend to instantiate Unity objects or deliver native UI events.
internal static class MixturePresentationRuntime
{
    internal static event System.Action? SessionEnding;
    internal static void EndTestSession() => SessionEnding?.Invoke();
}
internal static class MixtureInspection
{
    internal static void RegisterContainerOwner(GameObject owner, FluidContainerHandle handle, string label) { }
    internal static void UnregisterContainerOwner(GameObject owner, FluidContainerHandle handle) { }
}
