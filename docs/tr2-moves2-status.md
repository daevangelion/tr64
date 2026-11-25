# M10: climbing and water follow-ups in The Great Wall

Host only. Evidence: `$TR64_WORK/evidence/m10/moves2/`.
Rules are from the merged wall specifications and authored animation
links; schema-20 traces verify the runtime. The slice does not change
weapons, creatures, tick order or serialization. The existing producer
registers six new exact trace hashes, each with full camera coverage.

Reviewed v1 runtime and controls: `af9b9ffd2a07f0e525cc1a55a693e45f3007e5f5`.
This delta uses the specification amendments merged at `5d7ed33`.

## Climbing

[Q-T2M-03 and Q-T2M-04](specs/wall/tr2-climbing.md#leaving-a-lateral-ladder-traverse-at-an-edge-q-t2m-03)
replace the two old movement refusals. Marked ordinary hangs test the
current grip with the three narrow hand samples before trying the wider
stance pair. From a settled marked hang, that conversion has priority;
if it cannot convert, Back selects 188 and Forward can select 187 if the
ordinary pull-up is not available and the middle ceiling is clear. The
pull-up uses at least 762 world units at each sampled side, not 762
applied to an already body-relative difference. A marked release uses
the nearest root key and adds 256 down, keeping the nudged facing axis;
its following two-unit airborne motion takes the frontal correction
without the ordinary jump recoil.

At a lateral edge, the controller advances the authored transition to
hang in that tick, restores the preceding X/Z, and keeps the first new
frame. The body clip completes even after release of the side direction
while Action stays held, and can carry the queued ordinary shimmy. The
left exit uses the same authored transition mechanism; it is implemented
from the static link in 171 to 202 but has no retail route witness. The
upward jump collision admits exact front-ceiling tolerance at 192 and
side-only contact does not turn yaw.
The [stance rule](specs/wall/tr2-climbing.md#staying-on-moving-and-leaving)
requests Down after the ordinary advance, without a second advance:
`ledge-edge` 100 still samples 164 and requests 61; 101 enters 169.

## Water

[Wade step-up, frontal stop and return to the surface](specs/wall/tr2-water-and-flares.md#wade-step-up-and-frontal-stops-q-t2w-05)
share the absolute-frame Run/Wade step choice, with the extra 384-unit
front gate applying only to dry Run. Backward step-down uses the 964–993
window. A high flat Wade front can report state 12 while using neutral
11; the old starting-pose recovery otherwise still applies. On leaving
shallow water the already chosen Wade stride can run while Run is
requested. Submerged steering uses pitch, yaw rate and bank before the
stroke; the existing terrain probes keep their distinct front, side and
full-position rollback choices. On emerging to an air room near the
line, surface tread and the air increment apply in the same tick. An
executed current sink remains a named refusal before a trace seal.

[Water status](specs/wall/tr2-water-and-flares.md#water-status-before-movement)
clears both land and swim speeds on Wade-to-surface entry, before the
first stroke (`pool-swim` 584). The
[wet envelopes](specs/wall/tr2-water-and-flares.md#collision-envelopes-q-t2w-02)
use the shared cardinal front and two leading-corner samples with the
pre-contact travel heading. Submerged side response turns the body
but not that heading: at `pool-surface` 678 they are 25758 and 24848.

The common camera already obeys the
[low-eye retry](specs/wall/tr2-camera.md#move-the-eye-and-store-the-view):
`cameraMoveEye` supplies the resolved moved eye as the endpoint and
passes `&c->target` as the source; each `cameraSweep` initializes its
near point (including room) from that source. The returned endpoint
is then resolved and measured again. No camera change is needed; the
published retries do not distinguish the source and endpoint rooms.

[Q-T2W-07](specs/wall/tr2-water-and-flares.md#continuing-poses-in-shallow-water-q-t2w-07)
uses the mode already resolved for this tick. Back alone holds
backward Wade, whereas after a dry exit continuing Back also needs
Walk. In a retained Walk, Run or slow turn, eligible Forward requests
Wade irrespective of Walk; earlier death and admitted Run jump still
win. Walk still caps an ordinary turn, with fast turn from ready guns
taking precedence over the cap. Continuing Wade still requests Run
with Forward on its first dry update. The existing sidestep controller
continues only while the corresponding decoded request survives.

## Replay

| Route | Records with all Lara and camera fields equal | Checkpoint |
|---|---:|---|
| `ladder-hang-steps` | 225/225 | 188/5288 at 51, 187/5215 at 128, root-aligned release at 215 |
| `ledge-edge` | 256/256 | 201/5843 at 154, exit completes at 209, transverse-only contact at 220 |
| `wade-step` | 166/166 | Back alone keeps goal 16 on 40–70; release on 71 requests Stand while 61 advances; step-up 55/830 at 106 |
| `wade-wall` | 186/186 | State 12 with neutral 11/185 at 131 |
| `pool-exit` | 85/85 | Dry mode and one more Wade stride at 57 |
| `pool-surface` | 884/884 | Front stop at 669, surface and air 1711 at 709, stroke resets its counter at 721, bank rollback at 778 |

All six routes (1,802 records) match every Lara and camera field.
Gameplay RNG first differs at `pool-exit` 83 and `pool-surface` 430
because of the unimplemented creature updates; neither is masked.
All 30 earlier sealed artifacts (10,698 records) are unchanged.

The previous host fails the same directed replay on all six routes with
named refusals. Linked controls retain the near/far climb-clearance
thresholds, check Wade step-up and impact poses, and verify a current
still exits before sealing. Logs are in the evidence directory above.

The continuation controls exercise retained Walk and both turns with
Forward and either Walk state (not distinguished by the published
routes), plus the Run choice, death/jump precedence, turn-rate cap,
weapon priority, Wade release and the dry Back/Wade transitions.
`wade-step` 114 and `pool-swim` 464 exercise Run-without-Walk;
`pool-exit` 57 exercises continuing Wade becoming dry, without Walk.
The dedicated step hold/release is observed in `wade-wall` 40–87.
A host control switches to Walk+Right during that step, then releases
only Walk and checks that the continuing step requests Stand.
