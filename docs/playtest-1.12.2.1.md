# Mixed Atmospheres 1.12.2.1

The November 22 Windows log records a successful Cycle 55 autosave at frame
161779 with Every tick selected. The colony continued running until the
solver stopped on a horizontal momentum discrepancy. Frame 164706 was the
last completed tick. The error displayed both numbers as `0.000000`.

Restoring the colony's fluid state and advancing it without Unity reproduced
the failure on advance 2928, frame 164707. Replaying just the final step
isolated a nine-cell pocket. Its only moving horizontal face held
4.0453269870806629 × 10⁻³¹³ kg m/s. During the second flow substep, drag
booked an impulse that differed from the face's actual loss by
4.9406564584124654 × 10⁻³²⁴ kg m/s, one binary64 unit at that magnitude.
The absolute momentum operands summed to 8.0906539741119192 × 10⁻³¹³ kg m/s.
The existing relative conservation check correctly detected the discrepancy.

Drag now records the momentum the face actually gives up when its retained
momentum is subnormal. A separate test with heavier liquid pockets found
that converting momentum to velocity and back during transport could also
lose units. For these tiny momenta or velocities, each face retains its stored
momentum less the impulses its neighbours receive. Conservation errors now
print enough digits to distinguish small values from zero.

The original isolated step passed with the fix. Across 36 synthetic
partial-liquid cases spanning six masses and six small momenta, 1.12.2 failed
16 cases and 1.12.2.1 passed all 36. The hotfix reads the original save
directly and uses the existing owner and solver checkpoint formats.

Starting immediately before the failed tick, the corrected production solver
advanced the full colony for another 3,300 ticks. The new regression also
failed on 1.12.2 and passed on the hotfix. It checks both momentum axes, both
directions of travel, a face next to vacuum, a heavier face and the smallest
positive binary64 momentum. For each tick it compares the stored momentum
change with the wall impulse exactly.
