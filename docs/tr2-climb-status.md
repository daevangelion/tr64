# M10: Great Wall ledges, ladder and blocked movement

Host only. The rules come from the wall specifications and the authored
animations/sectors in `WALL.TR2`, not the sampled output. Evidence is under
`$TR64_WORK/evidence/m10/climb/`. The pack is the admitted Great Wall
`wall.pack`, SHA-256 `d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.
The schema-20 route manifests, placements, masks and row hashes are validated.

The independently reviewed v2 runtime and controls landed in
`014479bf4dd0733e5d24431e944498ad5dd5984d`. The acceptance record is
`review-v2/review-summary.txt` under the evidence directory.

## Rules

- [Ledges and shimmy](specs/wall/tr2-lara-ground-and-weapons.md#ordinary-vault-grab-and-shimmy):
  assisted jump earns its launch speed from the geometry, the advanced pose
  supplies the catch height, and release uses the **new** drop pose's lower
  bound. Pull-up is a post-advance request; the authored clip provides motion
  and hands-free timing. Run and sidestep no longer reset a blocked standing
  start to the first Stand frame: they replay the starting pose once. An
  established running frontal stop can select a splat from its cleared point
  and advanced frame. A standing sidestep begins before the collision check.
- [Climbable walls](specs/wall/tr2-climbing.md): floor function 6 grants the
  previous ordinary floor evaluation's eligible direction mark, sampled in
  `lara.climbing`. A height query does not change the mark. Mount and catch,
  ascent and descent use the specified hand samples, and a lateral step uses
  its retained travel heading before the approached hand is checked. A
  failed step restores the former horizontal position but keeps that test's
  heading. At the crest the settling advance consumes the trial height and
  the signed platform offsets before asking for the authored pull-up.
  Far marginal hang rejects the ceiling exactly 70 units above the span;
  the separate near probe still permits its exact 70-unit boundary. The
  climb-enabled floor scan keeps the first prefix entry even if lava
  follows, so it sees both a face mark and a following trigger.
- The scoped successor of `tr64-tr2-arms` is `tr64-tr2-climb`. It adds
  `lara.climbing`, keeps the old producer profiles unchanged, and inherits
  the arms and camera reviewed route boundaries (including all three
  placed climb/obstruction records through their last ticks). A numeric
  mismatch is not an availability exclusion.

## Baseline and directed controls

`rtk python3 test/game/tr2-climb-test.py HOST WALL FIXTURES --baseline-control`
against the v4 camera host names all three red cases: `ledge-grab` release
at 121, `ladder` climb-face mark at 1, and `sidestep-blocked`'s repeated
stand at 4. The final gate uses the same command without the baseline flag
and additionally demands camera, comparable Lara and gameplay RNG parity
through all five directed routes. The field replay also includes the other
movement fixtures, with no numeric difference ignored. Records include
tick zero.

The merged answers at `d7401c2` distinguish **Q-T2M-01** (a real lip can
catch a rising pose, but a marked click-band catch requires descent)
and **Q-T2M-02** (no separate Forward+Action facing push in Stand; its
starting position comes from authored motion and contact correction).
The first keeps `ladder` 21 airborne while still allowing the two
rising real-lip catches; the second removes the inherited four-unit
step at both pull-up exits. Debugger runs that suppressed the earlier
provisional catch are diagnostic only, never acceptance evidence.

The candidate is based on main `53e2fc6`, including the accepted arms
animation dispatcher. The frame-specific hands-free events are not
duplicated: the ledge and ladder paths advance their authored clips and
use the merged dispatcher for effect 12.

## Review controls

- `test/game/tr2-climb-geometry.c` checks near and far clearance at 69,
  70 and 71 in a linked 3×3 room. v1 returns EDGE instead of BLOCKED
  at far 70; v2 keeps EDGE at 69 and keeps the separate near threshold
  inclusive. The same linked test provokes a lateral edge in each
  direction: v1 attempts replacement with clip 96; v2 counts and names
  a refusal without replacing the side clip.
- `test/game/tr2-climb-mark-test.py` rebuilds integrity for synthetic WALL
  floor suffixes. In v1, `0106 0005 8004 0000 8000` loses its eligible
  mark. v2 retains it with each supported successor (0–6); the action
  count also verifies that a trigger after it still dispatches.
- `test/game/tr2-climb-test.py --hang-refusal-only` changes the published
  ladder approach's tick 52 to Back+Action. v1 seals a stationary hang;
  v2 stops at the request, with count 1 and no trace seal. The published
  Forward conversion through clip 172 still passes.

**Q-T2M-03** (authored side exit from 170/171) and **Q-T2M-04**
(one-grip moves 187/188) lack verified entry rules. A lateral hand edge
on the ladder, or a settled marked hang requesting Back or Forward
without a valid conversion to ladder stance, stops the host trace
with the named, counted refusal. These are not implemented exits and
are not reported as matching. Releasing Action and the established
ladder-stance conversion are not part of the refusal.

## Measured result

| Fixture | Equal camera, Lara and gameplay RNG records | Decisive checkpoints |
|---|---:|---|
| `ledge-grab` | 356/356 | assisted launch, rising catch and recatch, both shimmies, drop at 121, pull-up request at 226, hand release at 286 and exit at 302 |
| `ladder` | 852/852 | face mark at 1, rising rejection at 21, descending catch at 43, upward/downward settling, failed Left at 208 and retained heading into Right at 216, crest at 432, hands free at 532, run splat at 576 and first blocked stance recovery at 592 |
| `sidestep-blocked` | 79/79 | attempted movement before contact, established stop at 3, restored idle advance at 4, alias and opposite-side controls |
| `grab-attempt` | 252/252 | reach still rejects the lip |
| `look-sidestep` | 212/212 | no regression through Look admission, turning and decay |
| `stand`, `turn`, `travel`, `movement`, `jump`, `roll` | 1,433/1,433 | all other earlier movement fixtures |

Every serialized camera and Lara field and the gameplay random state
coincides across all 3,184 records. No camera range is cut in these
routes. The `lara.climbing` exclusion is retired for the new producer.
The unrelated `lara.timer`, `lara.vehicle` and `lara.weaponItem`
exclusions remain: their raw equality here is not an implementation
claim. The results are in `v2/final/summary.json` and `v2/final-replay.log`;
the red and green review controls are in `v2/`.
All 30 published schema-20 host traces are byte-identical to the
independently produced v1 traces, including their seals and camera
boundaries (`v2/all-v1-identity.log`).

The first-game invariant also passes: 25/25 solo traces byte-identical
in each schema 2–5, `.text` (437,744 B) and sized defined symbols identical
to the main shipping baseline, hot-layout window 16,288/16,384 B and
resident image 601,880/622,592 B. The candidate uses the same
`pc-campaign21-video.pack` shipping configuration and no emulator,
hardware, or TR2 cart. Evidence in `v2/`: `solo.log`,
`shipping-identity.log`, `hot.log`, and `size.log`.

The scope is the recorded on-foot paths. The two published lips are not
proof of ledge or ladder clearance for every room seam, and these
records do not exercise every attach, dismount, handstand, or lethal
release branch. The Combat view is a separate follow-up.
