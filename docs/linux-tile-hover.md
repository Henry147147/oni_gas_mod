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

The portable review protocol passes six new test groups, with 211 total Python
checks and four optional/platform skips. Actual Linux UI execution is pending
behind the current save/reload controls. Production fluid ownership and
conserved metabolism remain separate requirements.
