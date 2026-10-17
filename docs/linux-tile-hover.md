# Actual Linux mouse-hover acceptance

The native Linux runner exposes the same four-case `tile-hover` scene used by
the Windows UI control. It uses the real game window, pointer, SelectTool and
HoverTextDrawer. Native ONI retains physics; immutable fixture inventories
exercise presentation only.

```sh
python3 tools/oni_headless.py run --package /path/to/frozen/probe \
  --mode tile-hover --private-display --timeout 1800 \
  --output artifacts/linux-tile-hover-001
```

The scene prepares a visible arena, points the camera at its centre and reports
the screen coordinates in `tile-hover-contents.json` under the staged mod's
`game-contract-local/colony-validation` directory. Move the actual pointer over
that tile without selecting it. Three distinct hover frames must agree with
one immutable publication before the game captures its PNG and holds the case.

Review the actual screenshot, copy the held JSON to a private evidence file,
then publish the matching receipt. For case zero:

```sh
python3 tools/oni_tile_hover.py \
  --reviewed-report /path/to/held-case-zero.json \
  --output-directory /path/to/staged/colony-validation \
  --expected-case 0 --all-non-trace-rows-visible --trace-rows-omitted \
  --hazard-warning-visible --no-selection
```

The four switches attest the operator's image review. They must never be
inferred from the JSON. The writer records the exact reviewed report and PNG
hashes, then publishes a closed receipt atomically without overwrite. A
missing image, different dimensions, selected tile or stale case rejects
before continuation.

Repeat for the default four-gas list, exact trace threshold, just-below-threshold
case and 125% UI scale. Nitrogen, oxygen and CO2 always remain bulk rows. Steam
is bulk except immediately below the threshold. Benign hydrogen stays omitted;
hazardous chlorine stays omitted from bulk rows but retains its warning.
The game checks every displayed row against its completed mixture. The runner
requires all four final observations, their matching receipts, the retained
reviewed reports and unchanged screenshots. Its result keeps the display-fixture
scope and never sets first-playable or production-authority flags.

The portable review protocol now passes seven groups, with 215 total Python
checks and four optional/platform skips. Both platform adapters compile and
all 37 inspection and five native-description groups pass per declaration set.

The first actual Linux `linux-tile-hover-actual-v1` passes image review for the
default, exact-threshold and below-threshold cases. At 125% UI scale on a
1280-by-720 viewport, its original tooltip extends below the screen and hides
the mandatory chlorine warning. No continuation receipt was issued for that
case. The raw run remains failed, with its images and explicit pixel-review
failure retained.

The repair measures the game's completed shadow bars and selection borders in
the actual hover canvas. It translates text, icons and both borders together
inside that canvas, preserving font size, rows and warnings. A subsequent
native drawing restores the previous pool positions; session teardown also
retires the adjustment. Six new inspection groups cover the reproduced
geometry, all four edges, stable translation, oversized bounds, invalid inputs
and both installed native drawer contracts.

The actual repaired `linux-tile-hover-actual-v2` passes all four cases on Linux
ONI 744825 at 1280 by 720, using clean frozen `e43f543` and the same native
libraries as the failed baseline. Actual XTest pointer movement drives the
native hover path without selecting a cell. The images were individually
reviewed before publishing their bound receipts; the final report passes the
current strict portable reviewer. Completed revisions and publication
generations agree at 5, 8, 11 and 14.

All four bulk gases remain visible at the default threshold and at steam's
exact 0.04 threshold. At threshold 0.04000000000000001, steam becomes a trace
while the displayed nitrogen, oxygen and CO2 percentages remain unchanged.
Chlorine's warning remains visible in every case. The repaired 125% image keeps
the full warning and all four rows inside the viewport at the requested font
size. A separate actual capture after fixture cleanup shows the normal native
oxygen tooltip at 1200 g and 20 C, with the default UI scale and normal position
restored. The player closes, the private display retires and no mod failure
lines are present.

Private image review SHA-256:
`1dfada2b1e34873d78914543e9d5dd95461ed5707a89ecec39dad47928ddc241`.
The repaired raw result is
`97560e3e9d5a62cabf9da00103fafc6a35375dc1c2c26553fff3426faeea65de`;
the retained failed baseline is
`cf142d4d12b4592c7f531711afbb69f2bd52b5244e37ebcb44224ca050885075`.
Actual Windows acceptance of this viewport repair, production fluid ownership
and conserved metabolism remain separate requirements.

The merged layout also passes 37 inspection and five native-description groups
against the installed Windows game declarations. The Windows adapter builds
with no C# errors or warnings and the two existing MSB3277 reference conflicts.
These checks preserve the complete non-trace contents list and warning contract;
they do not replace image review in the repaired Windows game. Private evidence
is `local/windows-hover-layout-r1`, terminal-review SHA256
`97f88afa82c01fccf7815b5914f300e6a53ec4d2d3197c9aabe0c1279bd38525`.
