# Concurrent report writes

Run against the locally inspected game declarations:

```sh
dotnet run --project tools/ReportJsonTests -c Release -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
```

The build driver runs these controls when a game directory is supplied. They
link the production report writer and use synthetic text only: simultaneous
writers to one report, readers during replacement, temporary-file cleanup,
replacement failure, recovery and independent destinations. Temporary test
files live beside the ignored test executable and are removed afterward.

Serialization and complete writes share one bounded process-local gate. This
prevents world-generation and telemetry callbacks from colliding on the same
`.tmp` sibling. It does not coordinate separate processes. Concurrent readers
open with delete sharing so atomic replacement is permitted on Windows too.
The original writer reproduces the temporary-file collision in the private
release evidence. Actual game-Mono serialization and exception controls are
recorded separately; these .NET controls exercise the text-writing path.
