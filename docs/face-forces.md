# Face forces: the interface for fans and other mods

Since 1.13.0 Mixed Atmospheres lets another mod push the fluid on a cell face
of a colony it simulates. The other mod names a face and a fan curve; the
solver applies the force with everything else it does, so a fan builds a real
pressure difference, stalls against a sealed room and moves more air the less
it is resisted. [Mixed Atmospheres: Fans](../../oni_fan_mod) is the mod this
was built for.

- [The class](#the-class) and [how to bind it](#binding-it)
- [The force](#the-force)
- [Accounts: momentum, energy and heat](#accounts)
- [Lifetime, saves and faults](#lifetime)
- [How a building calls it](#how-a-building-calls-it)
- [What the solver does](#what-the-solver-does)
- [The layers below](#the-layers-below) and [the tests](#tests)

## The class

In the assembly `MixedAtmospheres.Oni`
([managed/MixedAtmospheres.Oni/FaceForces.cs](../managed/MixedAtmospheres.Oni/FaceForces.cs)):

```csharp
namespace MixedAtmospheres.Oni.Interop
{
    public static class FaceForces
    {
        public static int ApiVersion { get; }   // 1
        public static bool Active { get; }
        public static int Open();
        public static bool Set(int handle, int cell, int dx, int dy, double stallPressurePa, double freeSpeedMps);
        public static void Close(int handle);
        public static double TakeWork(int handle);

        public const double MaximumStallPressurePa = 10000;
        public const double MaximumFreeSpeedMps = 100;
    }
}
```

Names, parameter types and order are a contract. Later versions add members
and never change these, so a caller accepts any `ApiVersion` from the one it
was written for. Only primitives cross the interface.

| Member | Meaning |
| --- | --- |
| `ApiVersion` | `1`. |
| `Active` | True while the colony owner runs: the current colony's fluids belong to Mixed Atmospheres and face forces are applied. False at the main menu, in a colony that keeps the game's own simulation, and after a fluid fault. |
| `Open()` | A new handle (never 0) for one driven face, with no force yet. Returns 0 while not `Active`. |
| `Set(...)` | Drives the face between `cell` and the cell at `(dx, dy)` from it, from the next solver frame until the next `Set` or `Close` of that handle. Returns false, changing nothing, when the handle is not known. |
| `Close(handle)` | Stops and forgets the handle. Unknown handles are ignored. |
| `TakeWork(handle)` | Joules the handle's force did on the fluid in the solver frames completed since the previous call for that handle (or since `Open`). 0 for an unknown handle. Negative when fluid is driven backwards through the face. |

Arguments of `Set`:

- `cell` is the game's grid cell index (`Grid.PosToCell`).
- `(dx, dy)` is exactly one of `(1,0)`, `(-1,0)`, `(0,1)`, `(0,-1)`, with y
  upwards as in the game's grid. It names both the face and the direction of
  the push. The face between two cells has two names: `(cell, 1, 0)` pushes
  right through it and `(cell + 1, -1, 0)` pushes left through the same face.
- `stallPressurePa` is from 0 to 10 000 and `freeSpeedMps` from 0 to 100. With
  either 0 the handle applies no force and stays open. A value below 10⁻⁹
  counts as 0.
- A step that is not a unit step, or a strength that is negative, not finite or
  above its limit, is a caller's bug: `Set` throws `ArgumentException`
  (`ArgumentOutOfRangeException` for a strength). Nothing else throws.

A known handle whose face cannot be driven is not an error: `Set` returns true
and no force acts while that lasts. This covers a cell outside the colony's
rectangle or outside the grid, a solid cell or a solid neighbour, and a face
closed to flow. When the obstacle goes (a tile is dug out), the force starts
without another call.

The members may be called from the game's main thread at any time, for every
fan on every 200 ms tick. They take one small lock of their own and never wait
for the solver, which runs on its own thread.

## Binding it

Neither mod has to reference the other at compile time. Find the class by name
once every mod has loaded (`UserMod2.OnAllModsLoaded`) and bind delegates:

```csharp
Assembly gasMod = AppDomain.CurrentDomain.GetAssemblies()
    .FirstOrDefault(a => a.GetName().Name == "MixedAtmospheres.Oni");
Type type = gasMod?.GetType("MixedAtmospheres.Oni.Interop.FaceForces", false);
if (type == null) { /* Mixed Atmospheres is missing or older than 1.13.0 */ }

const BindingFlags flags = BindingFlags.Public | BindingFlags.Static;
int version = (int)type.GetProperty("ApiVersion", flags).GetValue(null);
var active = (Func<bool>)Delegate.CreateDelegate(typeof(Func<bool>), type.GetProperty("Active", flags).GetGetMethod());
var open = (Func<int>)Delegate.CreateDelegate(typeof(Func<int>), type.GetMethod("Open", flags));
var set = (Func<int, int, int, int, double, double, bool>)Delegate.CreateDelegate(
    typeof(Func<int, int, int, int, double, double, bool>), type.GetMethod("Set", flags));
var close = (Action<int>)Delegate.CreateDelegate(typeof(Action<int>), type.GetMethod("Close", flags));
var takeWork = (Func<int, double>)Delegate.CreateDelegate(typeof(Func<int, double>), type.GetMethod("TakeWork", flags));
```

The class exists in every game the mod loads in, whether or not a colony is
running; before the first Mixed Atmospheres colony of a session `Active` is
false and `Open` returns 0.

## The force

Let `u` be the fluid's velocity through the face, positive along `(dx, dy)`,
and `A` the area of the face: 1 m², a cell being one metre wide, high and deep.
The force on the fluid along `(dx, dy)` is

```
F = A · stallPressurePa · clamp(1 − u / freeSpeedMps, 0, 1)
```

A fan pushes with its full stall pressure against still or opposing fluid,
less as the fluid speeds up, and not at all once the fluid already moves at
the free speed. It never brakes fluid that is moving faster, and it never
pushes harder than its stall pressure against backflow. The most power it can
put into the fluid is `¼ · stallPressurePa · freeSpeedMps · A`, at half the
free speed.

- **All fluid on the face is pushed**, liquid as well as gas. Nothing is
  scaled by gas fraction. A cell that passes gas and not liquid (an Airflow
  Tile, or a building made like one) still lets no liquid through.
- **Closed faces get nothing.** Where nothing can cross a face, no force acts
  and no work is booked.
- **Several handles on one face add up**, each with its own curve. Two equal
  opposed handles cancel on still fluid; on moving fluid the one it flows
  against pushes with its whole stall pressure and the other with less, so
  together they brake.
- The force acts on the one named face. What happens in the rooms on either
  side is the solver's ordinary flow.

Two limits of the solver's time stepping are worth knowing. The force's branch
(backflow, the sloping part, above the free speed) is chosen once per flow
interval from the momentum the pressures at the start of the interval predict,
so within one interval a face that ends just outside its branch is pushed by
the formula of the branch it started in; the next interval corrects it. And a
fan at stall holds its pressure difference to within the solver's pressure
tolerance (in the tests, 120.0 Pa for 120 Pa).

## Accounts

Mixed Atmospheres closes a mass, momentum and energy ledger on every frame. A
face force is an input from outside the fluid, and is booked as one:

- **Momentum.** The force's impulse is an external load on the fluid, like a
  wall's.
- **Energy.** The force's work, impulse times the fluid's velocity through the
  face, is an external energy input. `TakeWork` returns exactly the joules the
  ledger booked for that handle. The work is negative when a larger pressure
  difference drives fluid backwards through the fan: the fluid then does work
  on the fan.
- **Heat.** Mixed Atmospheres adds only the work. A fan's motor heat is the
  fan mod's to add: a fan that draws `watts` should warm its own building with
  `watts · dt − TakeWork(handle)` each tick. That sum includes the negative
  work of backflow, which ends up as heat in the fan.

In the fluid, the fan's work becomes motion, and the solver's own drags turn
motion back into heat in the gas where it slows down.

## Lifetime

- **Nothing is saved.** Handles and their forces are not written to the
  colony's save, and the saved bytes do not depend on them. After a colony
  loads there are no handles; the fan mod opens them again and calls `Set`,
  and until it does no force acts.
- **Handles end with the colony owner.** When the colony is unloaded or the
  fluid simulation faults, every handle is forgotten: `Active` turns false,
  `Set` returns false and `Open` returns 0. When `Active` is true again, open a
  new handle.
- **A driven face is activity.** A face with a force on it keeps its region
  simulated every tick and wakes one the
  [activity controls](region-activity-controls.md) had paused. A handle with no
  force, or one whose face cannot be driven, wakes nothing.
- **The fluid update rate applies.** With Settings → Gameplay → Region
  simulation → Fluid update rate at 1 in N ticks, fluids move on every Nth
  tick only. Forces act in those ticks and `TakeWork` returns 0 for the others,
  so a fan's work and the pressure it builds are the same per fluid tick but
  less per game second. A fan mod that books `watts · dt − TakeWork` each tick
  stays correct: the electrical energy it did not hand to the fluid is heat.

## How a building calls it

For each fan, on every 200 ms simulation tick (`ISim200ms`):

```
if hook.Active and the fan is powered, enabled and not switched off:
    if no handle: handle = hook.Open()
    if not hook.Set(handle, cell, dx, dy, stall, speed):   # the colony changed
        handle = hook.Open(); hook.Set(handle, cell, dx, dy, stall, speed)
    heat the building with  watts · dt − hook.TakeWork(handle)
else:
    hook.Close(handle); handle = 0
```

Call `Close` when the building is destroyed as well. Calling `Set` with
unchanged values is cheap and is the intended way to notice a forgotten
handle. A change takes effect from the next solver frame.

Keep a step's peak air power, `¼ · stall · speed · A` with `A = 1 m²`, at or
below the watts it draws, or the fan would put more energy into the gas than
it takes from the wire.

## What the solver does

The production kernel advances each face's momentum `J` implicitly
([MAC_GAME_KERNEL.md](../native/MAC_GAME_KERNEL.md#face-forces-1130)):

```
J (1 + γ) = J_old + body + pressure impulse
```

with `γ` the sum of the kernel's drags. A face force has the same shape. With
`I` the face's inertia, `s = ±1` the direction of `(dx, dy)` on the face's
axis and `B = dt · A · stallPressurePa`:

| Branch | Fluid along the push | Added to the body impulse | Added to `γ` |
| --- | --- | --- | --- |
| Backflow | `u < 0` | `s · B` | nothing |
| Sloping | `0 ≤ u < freeSpeedMps` | `s · B` | `B / (I · freeSpeedMps)` |
| Free | `u ≥ freeSpeedMps` | nothing | nothing |

On the sloping branch that is the force taken implicitly in the new velocity,
so a strong fan on a thin gas cannot overshoot its free speed: in the test, a
120 Pa fan on cells holding 2 mg of oxygen ends at 5.6 of its 6 m/s. The
branch is the one that contains the solution of the face's own equation under
the pressures at the start of the interval; with several handles on a face the
equation is piecewise linear and never rising, and has one solution.

Unlike a drag, the `γ` term here is not dissipation. The force's impulse on
the fluid, `s · B − γ_fan · J`, goes to the momentum account as an external
load, and its work, that impulse times `J / I`, to the energy account as an
external input. The step's own pressure work, damping heat and drag heat are
booked as on any face.

With no handle the solver takes its old path: no value it computes differs
from a build without this feature.

## The layers below

| Layer | Where | What it adds |
| --- | --- | --- |
| Managed | `MixedAtmospheres.Oni.Interop.FaceForces` | The public class. Checks arguments, and answers false or 0 until the native library has started a colony in this session. |
| C exports | `ma_oni_colony_face_force_version`, `…_face_forces_active`, `…_face_force_open`, `…_face_force_set`, `…_face_force_close`, `…_face_force_take_work` in [oni_colony.h](../native/include/mixed_atmospheres/oni_colony.h) | The same calls without exceptions: `…_set` returns 0 (unknown handle), 1 (set) or 2 (invalid). None takes the backend's lifecycle lock. |
| Colony owner | `open_face_force`, `set_face_force`, `close_face_force`, `take_face_force_work` in [oni_colony.hpp](../native/src/oni_colony.hpp) | The handle registry. Each frame applies the handles driven at its start, wakes their regions, adds the work to the energy ledger's booked heat and credits each handle when the frame is committed. |
| Kernel | `MacGameKernelAccess::face_forces()` and `MacClosedAdvanceReport::game_face_force_work_j` | One advance's request and each force's work. |
| Pressure step | `PressureSolver::set_forces` in [pressure_be.hpp](../native/src/mac_game/pressure_be.hpp) | The force itself. |

The owner's [input journal](colony-owner.md) records the driven handles as a
`forces` line before the first frame that uses a changed set, so a journal
replay reproduces a session with fans bit for bit; a session without fans
writes no such line and its journal is unchanged.

The energy ledger books the work with the accepted external heat, so
`booked_heat_j` and the energy sample's `accepted_external_heat_j` include it.

## Tests

Game-free, run by `ctest`:

- `mac_game_face_forces` ([native/tests/mac_game_face_force_tests.cpp](../native/tests/mac_game_face_force_tests.cpp)):
  two sealed rooms joined by an opening with a 120 Pa fan in it settle 120 Pa
  apart with no flow and no further work; the same face pushed the other way;
  two handles on one face; a fan in a ring of duct drives 2.1 m/s and stays
  under ¼ · stall · speed; a stronger opposed fan drives gas backwards through
  a weak one, which then does negative work equal to its stall pressure times
  the flow; a face under water pushes the water; faces at a solid cell, at the
  grid's edge, with zero strength and closed to flow take no force and leave
  the world as it was; thin gas stays at or below the free speed; a checkpoint
  holds no force and a restored world continues exactly, cold and on 12
  workers; the single-region tick of the early profiles takes the same
  request. Every frame of every case checks that mass is exact and that U+K+G
  rose by the reported work.
- `oni_colony` (`face_force_control`, [native/tests/oni_face_force_control.hpp](../native/tests/oni_face_force_control.hpp)):
  the handles through the owner. Nothing is offered without an owner; invalid
  arguments are refused; undrivable cells are accepted and change nothing;
  a 120 Pa handle settles at 120 Pa and its `TakeWork` total equals the energy
  ledger's booked heat; open handles leave the checkpoint's bytes unchanged; a
  restored colony has none and relaxes; a handle set in a paused region wakes
  it; stopping and faulting end every handle.
- `oni_colony_replay_self`: a journaled session with a force set, changed and
  closed around a save replays byte for byte, cold and after a menu restore.

In the game:

```sh
tools/game_slot.sh low python3 tools/oni_headless.py run --package "$package" \
  --mode colony-owner --scenario face-forces --frames 1000 --speed 3 --dlc-mask 0 \
  --gpu-display :99 --extra-mod ../oni_fan_mod/dist/MixedAtmospheresFans --output "$output"
```

The `face-forces` scenario drives a face through the public class in a sealed
arena (stall pressure, work, refusals, closing), and, when the fan mod is
installed with `--extra-mod`, builds a Wall Fan in the opening and checks that
it runs, holds its step's stall pressure and warms. Without `--extra-mod` the
second part is reported as not run.
