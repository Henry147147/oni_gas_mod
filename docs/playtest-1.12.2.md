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

The save format, player settings and fluid update frequency are unchanged.
Measurements of alternative simulation settings use private copies of the
journal and save.
