# Native objects exchange heat with owned fluids

Native `SimTemperatureTransfer` objects use ONI's element-chunk system. Their
heat contact is a separate native writer from cell-pair conduction. This path
must be integrated for duplicants and loose resources inside an owned fluid
region; otherwise native heat changes the compatibility temperature behind
the physical owner's back. The first saved live-breather reload exposed this
missing writer at frame 2. Its failed evidence remains retained.

## Ownership and commit order

Observer ABI 4 adds the private element-chunk contact hook: 29 public slots,
25 concrete Windows locations and 29 Linux locations, with a 528-byte status.
Attachment checks the entire licensed native image, the entry fingerprint
and the pinned layout. Windows PDB inspection establishes the six-argument
procedure; the independently inspected GCC specialization has four arguments.
The 16-byte cell accessor carries the padded native cell index at offset 8.
The 44-byte chunk record carries temperature, heat capacity, conductivity and
area/thickness scale at offsets 4, 8, 12 and 16. No native STL object crosses
the public ABI, and no pointer is retained.

An outside or inactive contact calls the original native procedure unchanged.
An owned contact reads the completed fluid state plus prior accepted inputs,
prepares a conservative exchange, reserves its ordinary heat command and
journal entry, then changes the native object's temperature. It never writes
the native fluid temperature. ONI's original outer chunk loop still handles
object registration, adjusters, outside ground contacts, temperature publication
and `deltaKJ` publication. The worker advances the custom fluid after the
original `UpdateData` physics finishes and before `Step` publishes game data.
The frame fence covers both owners. A failed custom owner stays excluded from
native fluid writers and requires the normal failure recovery.

## Physical contact model

The native body's supplied binary32 heat capacity is converted from kJ/K to
J/K. Fluid conductivity uses actual EOS constituent volumes; body and fluid
conductivities use their harmonic mean, multiplied by the native
area/thickness and per-contact scales. This is the declared custom contact
law, rather than a claim to reproduce vanilla's heat-rate formula.

For conductance G, duration dt, body capacity C and heat q entering the fluid,
the implicit passive contact solves
`q/(dt*G) = T_body_before - q/C - T_fluid(U_before+q)`.
Single-phase constant-capacity models have an analytic solution. Gas/liquid
mixtures use the complete EOS and a bounded bracketed solve. Both directions
retain the temperature ordering and a nonnegative entropy lower bound.

The native body temperature is binary32. Its candidate temperature is rounded
toward its old value, and only the represented body energy loss is admitted to
the fluid. A contact too small for either endpoint leaves both unchanged and
increments an explicit unresolved counter. There is no minimum heat flux.
Fluid species, traces, passives, canonical momentum and time remain unchanged
by the contact itself. Each accepted exchange checks the full fluid/body
energy account at the existing 128-epsilon operand bound.

Completed additive capability `ma_oni_domain_chunk_heat_*`, version 1, reports
a 112-byte status: session/frame, contacts, transfers, unresolved contacts,
nonlinear iterations, represented body loss, stored fluid heat, balance bounds,
entropy lower bound and requested absolute heat. Pending contacts become
visible only when their physical frame commits. Accepted heat commands use
source `0x4d414348` and appear in the
[accepted-input journal](native-frame-inputs.md), preserving exact fluid replay
without predicting native object AI or body physiology.

## Verification and remaining work

The Linux native suite passes all 20 groups. Gas, filled-water and wet-mixture
contacts pass both heat directions, stiff limits, independent linear roots,
unrepresentable exchanges, ownership boundaries and exact journal replay.
Both managed game adapters compile. The 250 Python cases pass with four skips.

Four licensed SimDLL controls pass: gas and water on native Linux and the
Windows binary under Wine. Each runs 61 individually exact fluid replays and
56 real chunk contacts, retains native fluid compatibility tuples, checks the
original published object temperature and `deltaKJ`, and closes fluid U+K plus
represented body energy at its operand bound. The gas body cools from 350 K
to 346.82846 K; the water body reaches 312.0369 K. These are isolated native
worker tests, not Unity colony or actual Windows OS evidence.

The saved live-duplicant Unity reload must be rerun with this bridge. Bulk
solid-cell and building heat coupling, a complete material conductivity model,
phase-changing objects and conserved food/body/oxygen/CO2 metabolism remain
separate work. The bridge does not establish whole-colony authority.
