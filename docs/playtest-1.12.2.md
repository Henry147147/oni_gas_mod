# Mixed Atmospheres 1.12.2

This version targets the Windows playtest colony saved at frame 133995 on
November 21. At 2× the game must advance a 200 ms simulation tick in at most
100 ms of wall time. At 3× selected, 1.12.1 advanced 90.01 game seconds in
110.76 seconds across three consecutive unpaused windows on a Ryzen 9 5900X
running the Windows player under Proton.

Liquid transport can now divide a large set of vertical donor cells among
workers. Each donor still uses its own inventory and the same frozen face
sweeps. Donors that can deliver the full layered parcel skip the calculation
of the unused proportional parcel. The surface correction keeps its species
in sorted order and reuses the thermodynamic reading of a neighbouring cell
when its inventory and energy bits are unchanged. The pressure solver can
reuse a connectivity check while all active cells and open pressure faces
match. Each solve still uses the current conductances and checks the residual.

## Optional fluid update rate

Settings → Gameplay → Simulation → Fluid update rate controls how often the colony moves
its gases and liquids. Every tick is the default for new and existing saves.
At **1 in 8 ticks**, an awake room advances 0.2 seconds of fluid simulation
per 1.6 game seconds. The other choices are 1 in 4 and 1 in 16. Fluid-carried
heat, fluid-to-fluid conduction, phase changes and removal of traces follow
the selected rate. Duplicants, machines, pipe transfers, solid contacts and
world germ activity keep their game clocks. A pump can empty the tile by its
intake before surrounding fluid flows back to refill it.

The choice takes effect in a running colony. With a reduced rate the ONI
owner writes wrapper 15, including the interval. The saved frame count
determines the next fluid update after reload. Switching back to Every tick
resumes fluid updates immediately and the next save uses the ordinary
wrapper. The embedded MAC solver checkpoint uses the same format.
Measurements use private copies of the submitted save and its input journal.

This build includes the 1.12.1.1 pipe-heat save correction. In a recorded
one-in-eight replay, a drop entering sub-kelvin gas could stay below the
liquid's minimum temperature however far the flow interval was subdivided.
When that prevents any progress, the affected room retries from its opening
state with its scheduled fluid conduction first. Heat comes from neighbouring
fluid and the complete interval still has one conduction step. Both attempts
share the pressure budget and the same mass, energy and temperature checks.
