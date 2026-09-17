# Runtime behavior validation

Pending human-assisted session. Static G02 evidence establishes Classic fallback and dynamic resolver behavior; it does not establish physical runtime rendering or input behavior.

Required runtime evidence:

- Xbox Series X Controller connected and normal A/B/left-stick/Menu response.
- Existing message `0x100D` opens and progresses normally.
- CLASSIC visibly shows N64 Z.
- DYNAMIC visibly shows Xbox LT.
- LT still performs N64 Z behavior.
- No obvious stutter, reload, flicker, crash, or save mutation.

Device trace is `BEHAVIORALLY_VALIDATED` if the user confirms the Xbox connection and LT result but no diagnostic trace exposes device family/binding. It must not be reported as `TRACE_AVAILABLE` without real runtime output.
