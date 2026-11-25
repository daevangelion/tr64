# SPDX-License-Identifier: MIT
"""Cook-time Lara animation grafter for cross-game TR1/TR2/TR3 abilities.

Appends selected TR2 (wade, air/underwater 180 roll) and TR3 (sprint,
sprint-roll, crouch, crawl) Lara clips onto a TR1 or TR2 Level IR without
moving any existing animation index. Existing clips 0..N-1 stay untouched
except for new StateChange entries keyed by sequel goal states (65, 66, 68,
71, 73), leaving native classic behavior bit-exact when abilities are set
to the level's original game.
"""
from pathlib import Path
import src_tr2_tr3

# TR2 states & explicit helper clips to graft onto TR1 levels.
TR2_TARGET_STATES = frozenset((65, 66, 68))  # WADE, ROLL_UW, ROLL_AIR
TR2_SEED_CLIPS = frozenset((176, 177, 186, 190, 191, 192, 193, 203))
TR2_EXCLUDED_STATES = frozenset((56, 57, 58, 59, 60, 61, 67, 70))

# TR3 states to graft onto TR1 & TR2 levels (sprint, crouch, crawl).
TR3_TARGET_STATES = frozenset((71, 73, 74, 80, 81, 84, 85, 86))
TR3_SEED_CLIPS = frozenset((301,))  # Crawl death
TR3_EXCLUDED_STATES = frozenset((39, 67, 72, 75, 76, 77, 78, 79, 82, 83, 87, 88))
TR3_EXCLUDED_CLIPS = frozenset((218, 219, 220, 247, 289, 290, 291, 292, 302, 312))

# Safe AnimCommand effect IDs accepted by both TR1 and TR2 runtime validators.
SAFE_EFFECTS = frozenset((0, 3, 12))  # FX_ROTATE_180, BUBBLES, HANDSFREE
CMD_ARGS = (0, 3, 2, 0, 0, 2, 2)


def _find_lara_range(level):
    """Return (anim_base, anim_end) for Lara model type 0, or None."""
    models = level.data['models']
    m0 = next((m for m in models if m['type'] == 0), None)
    if m0 is None or m0['mesh_count'] != 15 or m0['animation'] == 65535:
        return None
    base = m0['animation']
    anims = level.data['animations']
    end = min((m['animation'] for m in models
               if m['animation'] != 65535 and m['animation'] > base),
              default=len(anims))
    return base, end


def _unpack_tr2_frame_to_tr1(frames, offset, stride, mesh_count=15):
    """Convert one TR2/TR3 packed frame into TR1's 10 + 2*mesh_count word layout."""
    out = list(frames[offset:offset + 9])  # 6 AABB + 3 root offset
    out.append(mesh_count)
    p = offset + 9
    end = offset + stride
    for _ in range(mesh_count):
        if p >= end:
            raise ValueError('lara_graft: truncated packed donor frame')
        w = frames[p] & 0xffff
        p += 1
        mode = w >> 14
        if mode == 0:
            if p >= end:
                raise ValueError('lara_graft: truncated 3-axis packed rotation')
            hi = w & 0x3fff
            lo = frames[p] & 0xffff
            p += 1
        else:
            v = w & 1023
            if mode == 1:
                hi = (v << 4) & 0x3ff0
                lo = 0
            elif mode == 2:
                hi = (v >> 6) & 15
                lo = (v & 63) << 10
            else:
                hi = 0
                lo = v
        # TR1 frameRotate reads angles[0]=lo, angles[1]=hi.
        out.append(lo)
        out.append(hi)
    return out


def _collect_donor_clips(donor, target_states, seed_clips, excluded_states,
                         excluded_clips, native_cutoff):
    """Collect transitive closure of donor clips >= native_cutoff and entry SCs."""
    span = _find_lara_range(donor)
    if span is None:
        raise ValueError(f'lara_graft: donor {donor.name} has no 15-bone Lara')
    base, end = span
    anims = donor.data['animations']
    states = donor.data['states']
    ranges = donor.data['ranges']

    selected = set()
    entry_scs = []  # (src_rel, state_id, [(low, high, dst_rel, dst_frame)])
    for i in range(base, end):
        rel = i - base
        a = anims[i]
        if rel >= native_cutoff and rel not in excluded_clips:
            if a['state'] in target_states or rel in seed_clips:
                selected.add(rel)
        if rel < native_cutoff:
            for s in states[a['state_offset']:a['state_offset'] + a['state_count']]:
                if s['state'] not in target_states:
                    continue
                rs = []
                for r in ranges[s['range_offset']:s['range_offset'] + s['range_count']]:
                    ta = r['next_animation'] - base
                    if ta in excluded_clips:
                        continue
                    if ta < 0 or base + ta >= end:
                        continue
                    if anims[base + ta]['state'] in excluded_states:
                        continue
                    rs.append((r['low'] & 0xffff, r['high'] & 0xffff, ta, r['next_frame'] & 0xffff))
                    if ta >= native_cutoff:
                        selected.add(ta)
                if rs:
                    entry_scs.append((rel, s['state'], rs))

    changed = True
    while changed:
        changed = False
        for rel in list(selected):
            a = anims[base + rel]
            na = a['next_animation'] - base
            if (na >= native_cutoff and na < end - base and na not in selected
                    and na not in excluded_clips
                    and anims[base + na]['state'] not in excluded_states):
                selected.add(na)
                changed = True
            for s in states[a['state_offset']:a['state_offset'] + a['state_count']]:
                if s['state'] in excluded_states:
                    continue
                for r in ranges[s['range_offset']:s['range_offset'] + s['range_count']]:
                    ta = r['next_animation'] - base
                    if (ta >= native_cutoff and ta < end - base and ta not in selected
                            and ta not in excluded_clips
                            and anims[base + ta]['state'] not in excluded_states):
                        selected.add(ta)
                        changed = True

    return base, sorted(selected), entry_scs


def _remap_frame(donor_anims, donor_base, donor_rel, donor_frame,
                 host_anims, host_idx):
    """Remap an absolute donor frame number onto the target host animation's span."""
    da = donor_anims[donor_base + donor_rel]
    ha = host_anims[host_idx]
    rel = donor_frame - da['frame_start']
    max_rel = ha['frame_end'] - ha['frame_start']
    if rel < 0:
        rel = 0
    elif rel > max_rel:
        rel = max_rel
    return ha['frame_start'] + rel


def _sanitize_tr1_terminal_range(level):
    """Neutralize TR1 retail unreachable sentinel range at ranges[-1] before appending."""
    anims = level.data['animations']
    states = level.data['states']
    ranges = level.data['ranges']
    if not anims or not ranges:
        return
    r = ranges[-1]
    a = anims[-1]
    if ((r['low'] & 0xffff) == 0 and (r['high'] & 0xffff) == 1
            and (r['next_animation'] & 0xffff) == len(anims)
            and (r['next_frame'] & 0xffff) == 1
            and a['frame_start'] == 0 and a['frame_end'] == 0
            and a['next_animation'] == len(anims) - 1 and a['next_frame'] == 0
            and a['state_count'] == 1 and a['state_offset'] < len(states)):
        s = states[a['state_offset']]
        if s['state'] == 1 and s['range_count'] == 1 and s['range_offset'] == len(ranges) - 1:
            ranges[-1] = dict(low=0, high=0, next_animation=len(anims) - 1, next_frame=0)


def _graft_from_donor(level, donor, target_states, seed_clips, excluded_states,
                      excluded_clips, native_cutoff, existing_map):
    """Append selected clips from donor onto level; returns {donor_rel: host_idx}."""
    game = level.reserved.get('game', 1)
    target_tr1 = (game == 1)
    host_anims = level.data['animations']
    host_states = level.data['states']
    host_ranges = level.data['ranges']
    host_commands = level.data['commands']
    host_frames = level.data['frames']
    host_sound_map = level.data['sound_map']
    host_has_footstep = len(host_sound_map) > 0 and host_sound_map[0] >= 0

    donor_base, clips, entry_scs = _collect_donor_clips(
        donor, target_states, seed_clips, excluded_states, excluded_clips, native_cutoff
    )
    donor_anims = donor.data['animations']
    donor_states = donor.data['states']
    donor_ranges = donor.data['ranges']
    donor_commands = donor.data['commands']
    donor_frames = donor.data['frames']

    host_base = _find_lara_range(level)[0]
    host_lara_end = _find_lara_range(level)[1]
    host_lara_count = host_lara_end - host_base

    # Phase 1: allocate new animation indices & frame spans, convert/copy frames.
    clip_map = dict(existing_map)
    next_frame_cursor = max((a['frame_end'] for a in host_anims), default=0) + 1
    new_meta = []  # (donor_rel, host_idx)
    for rel in clips:
        if rel in clip_map:
            continue
        da = donor_anims[donor_base + rel]
        span = da['frame_end'] - da['frame_start']
        rate = da['frame_rate']
        nframes = (span + rate - 1) // rate + 1
        src_off = da['frame_offset'] // 2
        stride = da['frame_size']
        frame_byte_off = len(host_frames) * 2
        if target_tr1:
            for k in range(nframes):
                host_frames.extend(_unpack_tr2_frame_to_tr1(
                    donor_frames, src_off + k * stride, stride, 15
                ))
            new_frame_size = 40
        else:
            host_frames.extend(donor_frames[src_off:src_off + nframes * stride])
            new_frame_size = stride
        span = da['frame_end'] - da['frame_start']
        f_start = next_frame_cursor
        f_end = f_start + span
        next_frame_cursor = f_end + 1

        # Filter commands for safety across TR1/TR2.
        cmd_off = len(host_commands)
        cmd_count = 0
        pos = da['command_offset']
        for _ in range(da['command_count']):
            op = donor_commands[pos] & 0xffff
            if op >= len(CMD_ARGS):
                break
            nargs = CMD_ARGS[op]
            args = [donor_commands[pos + 1 + j] for j in range(nargs)]
            pos += 1 + nargs
            if op in (1, 2, 3, 4):
                host_commands.append(op)
                host_commands.extend(args)
                cmd_count += 1
            elif op == 5:  # SOUND(frame, id)
                sid = args[1] & 0x3fff
                if sid == 0 and host_has_footstep:
                    rel_f = (args[0] & 0xffff) - da['frame_start']
                    rel_f = max(0, min(span, rel_f))
                    host_commands.extend((5, f_start + rel_f, 0))
                    cmd_count += 1
            elif op == 6:  # EFFECT(frame, id)
                eid = args[1] & 0x3fff
                if eid in SAFE_EFFECTS:
                    rel_f = (args[0] & 0xffff) - da['frame_start']
                    rel_f = max(0, min(span, rel_f))
                    host_commands.extend((6, f_start + rel_f, eid))
                    cmd_count += 1

        host_idx = len(host_anims)
        clip_map[rel] = host_idx
        host_anims.append(dict(
            frame_offset=frame_byte_off,
            frame_rate=da['frame_rate'],
            frame_size=new_frame_size,
            state=da['state'],
            speed=da['speed'],
            acceleration=da['acceleration'],
            frame_start=f_start,
            frame_end=f_end,
            next_animation=host_idx,  # resolved in phase 2
            next_frame=f_start,
            state_count=0,
            state_offset=len(host_states),
            command_count=cmd_count,
            command_offset=cmd_off if cmd_count else 0,
        ))
        new_meta.append((rel, host_idx))

    def resolve_target(donor_rel):
        if donor_rel in clip_map:
            return clip_map[donor_rel]
        if 0 <= donor_rel < host_lara_count:
            return host_base + donor_rel
        return None

    # Phase 2: resolve next_animation / next_frame and internal StateChanges.
    for rel, host_idx in new_meta:
        da = donor_anims[donor_base + rel]
        ha = host_anims[host_idx]
        na_rel = da['next_animation'] - donor_base
        na_host = resolve_target(na_rel)
        if na_host is None:
            # Fallback safely to Stand (103) if an excluded clip was referenced.
            na_host = host_base + 103
            ha['next_animation'] = na_host
            ha['next_frame'] = host_anims[na_host]['frame_start']
        else:
            ha['next_animation'] = na_host
            ha['next_frame'] = _remap_frame(
                donor_anims, donor_base, na_rel, da['next_frame'], host_anims, na_host
            )

        sc_start = len(host_states)
        sc_count = 0
        for s in donor_states[da['state_offset']:da['state_offset'] + da['state_count']]:
            if s['state'] in excluded_states:
                continue
            r_start = len(host_ranges)
            r_count = 0
            for r in donor_ranges[s['range_offset']:s['range_offset'] + s['range_count']]:
                ta_rel = r['next_animation'] - donor_base
                ta_host = resolve_target(ta_rel)
                if ta_host is None:
                    continue
                low = _remap_frame(donor_anims, donor_base, rel, r['low'] & 0xffff, host_anims, host_idx)
                high = _remap_frame(donor_anims, donor_base, rel, r['high'] & 0xffff, host_anims, host_idx)
                if low > high:
                    continue
                nf = _remap_frame(donor_anims, donor_base, ta_rel, r['next_frame'] & 0xffff, host_anims, ta_host)
                host_ranges.append(dict(low=low, high=high, next_animation=ta_host, next_frame=nf))
                r_count += 1
            if r_count:
                host_states.append(dict(state=s['state'], range_count=r_count, range_offset=r_start))
                sc_count += 1
        ha['state_offset'] = sc_start if sc_count else 0
        ha['state_count'] = sc_count

    # Phase 3: append entry StateChanges onto existing native host clips (< native_cutoff).
    by_src = {}
    for src_rel, state_id, rs in entry_scs:
        if src_rel >= host_lara_count:
            continue
        by_src.setdefault(src_rel, []).append((state_id, rs))

    for src_rel, items in by_src.items():
        ha = host_anims[host_base + src_rel]
        existing_scs = list(host_states[ha['state_offset']:ha['state_offset'] + ha['state_count']])
        existing_states = {s['state'] for s in existing_scs}
        added = []
        for state_id, rs in items:
            if state_id in existing_states:
                continue
            r_start = len(host_ranges)
            r_count = 0
            for low_d, high_d, ta_rel, nf_d in rs:
                ta_host = resolve_target(ta_rel)
                if ta_host is None:
                    continue
                low = _remap_frame(donor_anims, donor_base, src_rel, low_d, host_anims, host_base + src_rel)
                high = _remap_frame(donor_anims, donor_base, src_rel, high_d, host_anims, host_base + src_rel)
                if low > high:
                    continue
                nf = _remap_frame(donor_anims, donor_base, ta_rel, nf_d, host_anims, ta_host)
                host_ranges.append(dict(low=low, high=high, next_animation=ta_host, next_frame=nf))
                r_count += 1
            if r_count:
                added.append(dict(state=state_id, range_count=r_count, range_offset=r_start))
                existing_states.add(state_id)
        if added:
            new_sc_off = len(host_states)
            host_states.extend(existing_scs)
            host_states.extend(added)
            ha['state_offset'] = new_sc_off
            ha['state_count'] = len(existing_scs) + len(added)

    return clip_map


def load_donor(path: Path):
    """Load a PC TR2 or TR3 donor level for Lara animation grafting (no SFX bank needed)."""
    return src_tr2_tr3.load(path, bank=None, census=True)


def graft_lara_abilities(level, tr2_donor=None, tr3_donor=None):
    """Graft TR2 and/or TR3 Lara ability clips onto a TR1 or TR2 Level IR in place.

    Returns a summary dict, or None if the level has no playable Lara model.
    """
    game = level.reserved.get('game', 1)
    if game not in (1, 2):
        return None
    span = _find_lara_range(level)
    if span is None:
        return None
    base, end = span
    lara_count = end - base
    if (game == 1 and lara_count < 160) or (game == 2 and lara_count < 218):
        return None

    # Ensure lists are mutable copies.
    for key in ('animations', 'states', 'ranges', 'commands', 'frames'):
        if not isinstance(level.data[key], list):
            level.data[key] = list(level.data[key])
    level.data['animations'] = [dict(a) for a in level.data['animations']]
    level.data['states'] = [dict(s) for s in level.data['states']]
    level.data['ranges'] = [dict(r) for r in level.data['ranges']]

    if game == 1:
        _sanitize_tr1_terminal_range(level)

    orig_anims = len(level.data['animations'])
    orig_frames = len(level.data['frames'])
    clip_map = {}

    if game == 1 and tr2_donor is not None:
        clip_map = _graft_from_donor(
            level, tr2_donor, TR2_TARGET_STATES, TR2_SEED_CLIPS,
            TR2_EXCLUDED_STATES, frozenset(), 160, clip_map
        )

    if tr3_donor is not None:
        clip_map = _graft_from_donor(
            level, tr3_donor, TR3_TARGET_STATES, TR3_SEED_CLIPS,
            TR3_EXCLUDED_STATES, TR3_EXCLUDED_CLIPS, 217, clip_map
        )

    return {
        'added_animations': len(level.data['animations']) - orig_anims,
        'added_frame_words': len(level.data['frames']) - orig_frames,
        'added_frame_bytes': (len(level.data['frames']) - orig_frames) * 2,
    }
