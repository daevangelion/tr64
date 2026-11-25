#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Synthetic log controls for the production shipping sound predicate."""
import hashlib
import json
import os
import shutil
import subprocess
import unittest
import sys
sys.dont_write_bytecode = True
import tempfile
from pathlib import Path
from unittest import mock
from boot import (MAX_PRESSES, CURSOR_RETRY_FRAMES, SAVE_SLOT, SAVE_STEPS, ShotPruner, catalog, cursor_step,
                  cutscene_path, pack_video_names, sound_evidence)
from boot import smoke_film_input
from boot import account_sound, accounted_sound_verdict
import deliver_check
import gate_logs
import gate_runs
import perf_overlay

AUDIO = "audio: ticks=100 events=2 played=1 underruns=0 frames=32 mixer_us=300 mixer_peak_buffer_us=100\n"
OUTPUT = "audio-output: cart rate=22050 frames=64 underruns=0 pumps=4 mixer_total_us=900 mixer_peak_pump_us=200\n"
READY = "play: ready=level1 hz=30\n"
LOG = READY + (AUDIO + OUTPUT) * 3
BUILD = "0500E430EC0B"
CONFIG = "ab" * 32
GATE_BUILD, GATE_CONFIG = "F00DE430EC0B", "cd" * 32
BUILD_LINE = f"play: build_id={BUILD} config_id={CONFIG} mode=live\n"
PACK = hashlib.sha256(b"pack").hexdigest()
PACK_LINE = f"level-pack: sha256={PACK}\n"
IDENTITY = BUILD_LINE + PACK_LINE
EXPECTED = BUILD, CONFIG, PACK


class SoundGate(unittest.TestCase):
    def test_positive_and_metrics(self):
        result = sound_evidence(LOG, 3)
        self.assertTrue(result["pass"])
        self.assertEqual(result["mixer_peak_buffer_us"], 100)
        self.assertEqual(result["mixer_peak_pump_us"], 200)
        self.assertEqual(len(result["reports"]), 3)

    def test_valid_zero_boot_prefix_is_not_a_play_report(self):
        silent = AUDIO.replace("ticks=100", "ticks=0").replace("events=2", "events=0").replace("played=1", "played=0").replace("frames=32", "frames=0")
        output = OUTPUT.replace("frames=64", "frames=0")
        self.assertTrue(sound_evidence(silent + output + LOG, 3)["pass"])

    def test_late_zero_report_is_not_discarded(self):
        self.assertFalse(sound_evidence(LOG + OUTPUT.replace("frames=64", "frames=0"), 3)["pass"])

    def test_boot_is_not_played_audio(self):
        self.assertFalse(sound_evidence("audio-output: cart rate=22050 buffers=4\n", 3)["pass"])

    def test_insufficient_reports(self):
        self.assertFalse(sound_evidence(AUDIO + OUTPUT, 3)["pass"])
        self.assertFalse(sound_evidence("", 3)["pass"])

    def test_every_required_field(self):
        for field in ("ticks=100", "events=2", "played=1", "underruns=0", "frames=32",
                      "mixer_us=300", "mixer_peak_buffer_us=100"):
            with self.subTest(source="mixer", field=field):
                self.assertFalse(sound_evidence(LOG.replace(field, "", 1), 3)["pass"])
        for field in ("frames=64", "underruns=0", "mixer_total_us=900", "mixer_peak_pump_us=200"):
            with self.subTest(source="output", field=field):
                changed = READY + AUDIO + OUTPUT.replace(field, "", 1) + (AUDIO + OUTPUT) * 2
                self.assertFalse(sound_evidence(changed, 3)["pass"])

    def test_events_and_output_must_be_real(self):
        for old, new in (("ticks=100", "ticks=0"), ("events=2", "events=0"),
                         ("played=1", "played=0"), ("frames=32", "frames=0"),
                         ("frames=64", "frames=0")):
            with self.subTest(field=old):
                self.assertFalse(sound_evidence(LOG.replace(old, new, 1), 3)["pass"])

    def test_stream_underrun_is_rejected(self):
        self.assertFalse(sound_evidence(LOG.replace("underruns=0", "underruns=1", 1), 3)["pass"])

    def test_output_underrun_is_rejected(self):
        changed = READY + AUDIO + OUTPUT.replace("underruns=0", "underruns=1") + (AUDIO + OUTPUT) * 2
        self.assertFalse(sound_evidence(changed, 3)["pass"])

    def test_nonterminal_reports_reject_malformed_or_duplicate_counters(self):
        for counter in ("underruns=0oops", "underruns=1 underruns=0"):
            for audio, output in ((AUDIO, OUTPUT.replace("underruns=0", counter)),
                                  (AUDIO.replace("underruns=0", counter), OUTPUT)):
                with self.subTest(counter=counter, audio=audio, output=output):
                    log = READY + audio + output + (AUDIO + OUTPUT) * 3
                    self.assertFalse(sound_evidence(log, 3)["pass"])


class SoundPacketNotices(unittest.TestCase):
    # Route and report excerpt from evidence/m9/ship-invalid-20b8100/ares.log,
    # with unrelated lines elided; the warning and its neighbors are verbatim.
    LOG = """play: loading=title
play: ready=title hz=30
play-stop: level=title kind=2 next_level_id=2 death_ready=0 name=game level transition UI not ported
play-pace: level us=36954446 tick=102 frames=102 ticks=102 hist=102/0/0/0 dropped=0 restarts=0
music: starts=1 loops=0 unavailable=0 underruns=0 frames=75264 reads=13 bytes=49216 arena=19984 mix_us=70378 peak_buffer_us=607 frame_peak_us=1206 startup_peak_us=11998 codec=1 rate=22050 current=-1 phase=0
audio: ticks=102 events=1 played=1 dropped=0 unavailable=0 underruns=0 clipped=0 frames=792064 resident=13168 resident_capacity=25968 arena=98304 stream_reads=4 stream_bytes=14384 mixer_us=1155220 mixer_peak_buffer_us=976
audio-ambient: sources=0 started=0 stopped=0
audio-output: cart rate=22047 frames=808640 underruns=0 pumps=866458 mixer_total_us=1240219 mixer_peak_pump_us=1266
audio-time: active_us=35975199
play: loading=level1
play: ready=level1 hz=30
play-select: from=level1 to=level1 command=1
play-pace: level us=73395512 tick=623 frames=521 ticks=521 hist=521/0/0/0 dropped=0 restarts=1
music: starts=2 loops=0 unavailable=0 underruns=0 frames=459648 reads=69 bytes=274560 arena=19984 mix_us=424430 peak_buffer_us=620 frame_peak_us=1206 startup_peak_us=11998 codec=1 rate=22050 current=-1 phase=0
audio: ticks=623 events=12 played=12 dropped=0 unavailable=0 underruns=0 clipped=0 frames=1572032 resident=25392 resident_capacity=25968 arena=98304 stream_reads=20 stream_bytes=71920 mixer_us=2323379 mixer_peak_buffer_us=1234
audio-ambient: sources=0 started=0 stopped=0
audio-output: cart rate=22047 frames=1607424 underruns=0 pumps=1767079 mixer_total_us=2482193 mixer_peak_pump_us=1799
audio-time: active_us=71313827
play: loading=level1
play: ready=level1 hz=30
play-select: from=level1 to=level2 command=3
play-pace: level us=75394393 tick=655 frames=33 ticks=33 hist=33/0/0/0 dropped=0 restarts=0
VI WARNING: __vblank_interrupt outside of vblank period: 12
music: starts=3 loops=0 unavailable=0 underruns=0 frames=484736 reads=75 bytes=295104 arena=19984 mix_us=447550 peak_buffer_us=620 frame_peak_us=1206 startup_peak_us=11998 codec=1 rate=22050 current=-1 phase=0
audio: ticks=655 events=12 played=12 dropped=0 unavailable=0 underruns=0 clipped=0 frames=1597120 resident=25392 resident_capacity=25968 arena=98304 stream_reads=20 stream_bytes=71920 mixer_us=2358821 mixer_peak_buffer_us=1234
audio-ambient: sources=0 started=0 stopped=0
audio-output: cart rate=22047 frames=1646848 underruns=0 pumps=1821462 mixer_total_us=2522473 mixer_peak_pump_us=2016
audio-time: active_us=72412288
play: loading=level2
"""
    STOPS = [("title", "level1", None), ("level1", "level1", 1), ("level1", "level2", 3)]
    WARNING = "VI WARNING: __vblank_interrupt outside of vblank period: 12\n"
    SHOT = "[ares] screenshot test.ppm (320x240 bpp2 fb=0x3a5000 tag=t0144)\n"

    def test_real_warning_between_stop_rows_preserves_sound_verdict(self):
        samples, reason = account_sound(self.LOG, self.STOPS)
        self.assertIsNone(reason)
        self.assertTrue(accounted_sound_verdict(samples, 2, reason)["pass"])

    def test_complete_foreign_lines_between_all_required_rows(self):
        before, after = self.LOG.split(self.WARNING)
        rows = after.splitlines(keepends=True)
        notices = ["TESTHARNESS echo\n", self.SHOT, self.WARNING, "a harmless\tguest diagnostic\n", "\n"]
        changed = before + "".join(notice + row for notice, row in zip(notices, rows[:5])) + "".join(rows[5:])
        self.assertIsNone(account_sound(changed, self.STOPS)[1])

    def test_screenshot_inside_any_required_row_still_invalidates(self):
        before, after = self.LOG.split(self.WARNING)
        before, pace = before[:-1].rsplit("\n", 1)
        before += "\n"
        rows = [pace + "\n", *after.splitlines(keepends=True)]
        for i, row in enumerate(rows[:6]):
            for split in range(1, len(row)):
                with self.subTest(row=i, offset=split):
                    torn = row[:split] + self.SHOT + row[split:]
                    changed = before + "".join(rows[:i]) + torn + "".join(rows[i + 1:])
                    self.assertIsNotNone(account_sound(changed, self.STOPS)[1])
            with self.subTest(deleted=i):
                changed = before + self.SHOT + "".join(rows[:i] + rows[i + 1:])
                self.assertIsNotNone(account_sound(changed, self.STOPS)[1])

    def test_foreign_line_with_report_prefix_or_token_is_not_transparent(self):
        for token in ("audio-output: cart", "audio-outp", "usic:", "audio-time:", "mus"):
            with self.subTest(token=token):
                changed = self.LOG.replace(self.WARNING, f"notice about {token} in the log\n")
                reason = account_sound(changed, self.STOPS)[1]
                self.assertIn(f"notice about {token}", reason)

    def test_foreign_line_cannot_bridge_two_stop_packets(self):
        first, second = self.LOG.rsplit("play: loading=level1\n", 1)
        head, tail = first.rsplit("audio-output: cart ", 1)
        route, packet = second.split(self.WARNING)
        moved = head + "play: loading=level1\n" + route + self.WARNING + "audio-output: cart " + tail + packet
        reason = account_sound(moved, self.STOPS)[1]
        self.assertIn("expected intact audio-output:", reason)
        self.assertIn("play: loading=level1", reason)


class CutsceneGate(unittest.TestCase):
    ORDER = catalog(Path(__file__).resolve().parents[2])

    def test_catalog_is_the_campaign_order_with_cutscenes(self):
        order = self.ORDER
        self.assertEqual(len(order), 20)
        for cut, before, after in (("cut1", "level3b", "level4"), ("cut2", "level7b", "level8a"),
                                   ("cut3", "level10a", "level10b"), ("cut4", "level10b", "level10c")):
            self.assertEqual(order[order.index(cut) - 1], before if cut != "cut4" else "level10b")
            self.assertEqual(order[order.index(cut) + 1], after)

    def test_paths_end_at_the_cutscene_and_are_shortest(self):
        order = self.ORDER
        for target, steps in (("cut4", 3), ("cut2", 10), ("cut1", 4), ("cut3", 5)):
            path = cutscene_path(order, target)
            self.assertEqual(order[path[-1][2]], target)
            self.assertEqual(len(path), steps)

    def test_only_cutscenes_are_accepted(self):
        with self.assertRaises(SystemExit):
            cutscene_path(self.ORDER, "level2")

    def test_pruner_keeps_level_starts_and_a_sample(self):
        with tempfile.TemporaryDirectory() as tmp:
            names, lines = [], ["play: ready=level1 hz=30\n"]
            for i in range(12):
                path = Path(tmp, f"s{i}.ppm")
                path.write_bytes(b"x")
                names.append(path)
                lines.append(f"[ares] screenshot {path} (320x240 bpp2 fb=0x0 tag=t{i})\n")
            ShotPruner().run("".join(lines))
            self.assertEqual([p.exists() for p in names],
                             [i < 2 or (i + 1) % 5 == 0 for i in range(12)])


def lara(kind, tick, pos=(1000, 500, 2000), health=1000, room=3):
    return (f'play-lara: {kind} tick={tick} {{"schema":2,"tick":{tick},"lara":{{"index":0,"type":0,'
            f'"pos":[{pos[0]},{pos[1]},{pos[2]}],"rot":[0,16384,0],"room":{room},"anim":103,"frame":0,"state":2,'
            f'"goal":2,"next":2,"flags":0,"health":{health},"hSpeed":0\n')


def menu(frame, state, page, passport=0):
    return f"ui: menu frame={frame} state={state} page={page} passport={passport} item=0\n"


def frames(first, count, actions=0):
    return "".join(f"play-frame: tick={first + i} input={actions:04x} tick_us=500\n" for i in range(count))


class LaraLog(unittest.TestCase):
    def test_samples_and_the_saved_lara_is_the_loaded_one(self):
        saved, restored = gate_logs.saved_and_restored(lara("tick", 10) + lara("saved", 90, (5, 6, 7)),
                                                       lara("restored", 3, (5, 6, 7)))
        self.assertEqual(saved["pos"], (5, 6, 7))
        self.assertTrue(gate_logs.same_lara(saved, restored))

    def test_a_different_position_room_or_health_is_not_the_saved_lara(self):
        saved = gate_logs.lara_samples(lara("saved", 90, (5, 6, 7)))[0]
        for other in (lara("restored", 3, (5, 6, 8)), lara("restored", 3, (5, 6, 7), room=4),
                      lara("restored", 3, (5, 6, 7), health=999)):
            with self.subTest(other=other[:40]):
                self.assertFalse(gate_logs.same_lara(saved, gate_logs.lara_samples(other)[0]))

    def test_no_saved_or_restored_line_is_no_evidence(self):
        self.assertEqual(gate_logs.saved_and_restored(lara("tick", 10), lara("tick", 10)), (None, None))

    def test_title_has_no_lara(self):
        title = 'play-lara: tick tick=10 {"schema":2,"tick":10,"lara":null,"camera":null\n'
        self.assertIsNone(gate_logs.lara_samples(title)[0]["pos"])

    def test_displacement_is_lara_not_the_camera(self):
        samples = gate_logs.lara_samples(lara("tick", 30, (0, 0, 0)) + lara("tick", 40, (300, 9, 400)) +
                                         lara("tick", 90, (9999, 0, 0)))
        self.assertEqual(gate_logs.displacement(samples, 30, 80), 500.0)
        self.assertIsNone(gate_logs.displacement(samples, 35, 80))  # one sample is no displacement
        still = gate_logs.lara_samples(lara("tick", 30) + lara("tick", 40) + lara("tick", 50))
        self.assertEqual(gate_logs.displacement(still, 20, 80), 0.0)


class DeathEndChecks(unittest.TestCase):
    NEXT = "level2"

    def run_log(self, **changes):
        """A full synthetic base: death, waited, restarted, end placed, statistics page, START, next level."""
        o = dict(dies=True, waits=True, restart_stop=True, alive=True, ring_restart=False, end_ok=True, stats_waits=True,
                 start_stop=True, next_ring=False, select=False, ticks=9, ready=True, interrupted=False)
        o.update(changes)
        text = "play: ready=title hz=30\nplay: ready=level1 hz=30\n" + lara("tick", 20)
        text = IDENTITY + text
        mark = {}
        text += "gate: place death ok=1\n"
        mark["placed_death"] = len(text)
        text += lara("tick", 30, health=0 if o["dies"] else 1000)
        text += menu(40, 9, 1, 1)
        mark["death_page"] = len(text) - len("item=0\n")
        text += frames(31, 45 if o["waits"] else 5)
        if o["interrupted"]:
            text += menu(80, 4, 1, 1)
        mark["death_wait_end"] = len(text)
        if o["restart_stop"]:
            text += "play-stop: level=level1 kind=2 next_level_id=2 death_ready=0 name=x\n"
        text += "play: ready=level1 hz=30\n"
        mark["restarted"] = len(text)
        text += lara("tick", 100, health=1000 if o["alive"] else 0)
        if o["ring_restart"]:
            text += menu(110, 7, 5)
        text += "gate: place end ok=1\n" if o["end_ok"] else "gate: place end ok=0\n"
        mark["placed_end"] = len(text)
        text += menu(120, 9, 3)
        mark["stats"] = len(text) - len("item=0\n")
        text += frames(121, 30 if o["stats_waits"] else 3)
        mark["stats_wait_end"] = len(text)
        if o["start_stop"]:
            text += "play-stop: level=level1 kind=2 next_level_id=3 death_ready=0 name=x\n"
        text += f"play: ready={self.NEXT} hz=30\n" if o["ready"] else ""
        mark["next_ready"] = len(text)
        text += frames(151, o["ticks"], 0x800 if o["select"] else 0)
        mark["start_released"] = len(text)
        if o["next_ring"]:
            text += menu(170, 1, 0)
        text += frames(160, 30)
        return text, mark

    def checks(self, **changes):
        text, mark = self.run_log(**changes)
        return gate_runs.death_end_checks(text, mark, self.NEXT)

    def test_a_good_run_passes(self):
        checks = self.checks()
        self.assertTrue(all(checks.values()), {k: v for k, v in checks.items() if not v})
        self.assertGreaterEqual(len(checks), 15)

    def test_every_broken_behaviour_is_caught(self):
        for change, check in (({"dies": False}, "dies"), ({"waits": False}, "death_page_waits"),
                              ({"interrupted": True}, "death_page_waits"),
                              ({"restart_stop": False}, "restart_is_a_level_end"),
                              ({"alive": False}, "alive_after_restart"),
                              ({"ring_restart": True}, "no_ring_after_restart"),
                              ({"end_ok": False}, "end_placed"),
                              ({"stats_waits": False}, "statistics_page_waits"),
                              ({"start_stop": False}, "start_ends_the_level"),
                              ({"next_ring": True}, "no_ring_in_next_level"),
                              ({"select": True}, "no_select_while_start_held"),
                              ({"ticks": 2}, "next_level_ticks_with_start_held"),
                              ({"ready": False}, "levels_title_level1_restart_next")):
            with self.subTest(change=change):
                self.assertFalse(self.checks(**change)[check])

    def test_a_run_that_never_reached_a_step_has_no_checks_for_it_and_fails(self):
        text, mark = self.run_log()
        short = {k: v for k, v in mark.items() if k in ("placed_death", "death_page")}
        checks = gate_runs.death_end_checks(text[:mark["death_page"]], short, self.NEXT)
        self.assertFalse(gate_runs.verdict(text, EXPECTED, checks, None)["pass"])
        self.assertFalse(gate_runs.verdict(text, EXPECTED, {}, None)["pass"])
        self.assertFalse(gate_runs.verdict(text, EXPECTED, {"x": True}, "deadline while waiting")["pass"])


def pace_log(frame_us, *, legacy=False, seconds=15, burn=0, music=True, audio_scale=1.0, speed=39):
    """A gate-ROM log of Lara running in a circle: frames of `frame_us` microseconds of the clock. The accurate loop
    runs the ticks the clock owes (30 a second, at most four a frame); the legacy loop runs one a frame."""
    lines, t, tick, path, granted, next_audio, frames = ["gate: place run ok=1\n"], 1_000_000, 0, 0, 0, 0, 0
    start, moving_at = t, t + 300_000
    while t - start < seconds * 1_000_000:
        due = 1 if legacy else min(4, max(1, (t - start) * 30 // 1_000_000 - granted))
        granted += due if not legacy else 0
        if legacy:
            granted = tick + due
        tick += due
        frames += 1
        if t >= moving_at:
            path += speed * due
        lines.append(f"gate-run: tick={tick} us={t} path={path} burn_ms={burn}\n")
        if tick % 30 < due:
            lines.append(f"play-pace: tick us={t} tick={tick} frames={frames} ticks={tick} hist={frames}/0/0/0 "
                         f"dropped=0 restarts=0\n")
        if tick >= next_audio and t > moving_at:
            next_audio = tick + 60
            elapsed = t - start
            lines.append(f"gate-audio: us={t}\n")
            lines.append(f"audio-output: cart rate=22050 frames={int(elapsed * 22050 / 1e6 * audio_scale)} "
                         "underruns=0 pumps=9 mixer_total_us=1 mixer_peak_pump_us=1\n")
            if music:
                lines.append(f"music: starts=1 loops=0 unavailable=0 underruns=0 frames={int(elapsed * 22050 / 1e6 * audio_scale)} "
                             "reads=1 bytes=1 arena=1 mix_us=1 peak_buffer_us=1 frame_peak_us=1 startup_peak_us=1 "
                             "codec=1 rate=22050 current=3 phase=2\n")
        t += frame_us
    return "".join(lines)


class PaceGate(unittest.TestCase):
    MARK = {"begin": 0}

    def window(self, text, **kw):
        return gate_logs.pace_window(text, 0, None, **kw)

    def test_accurate_loop_follows_the_clock_at_any_frame_cost(self):
        for frame_us in (33_333, 40_000, 73_000, 100_000):
            with self.subTest(frame_us=frame_us):
                w = self.window(pace_log(frame_us))
                self.assertTrue(w["ticks_follow_clock"], w)
                self.assertEqual(w["dropped"], 0)
                self.assertTrue(w["moved"])
                self.assertTrue(w["audio"]["mixer_follows_clock"] and w["audio"]["music_follows_clock"])
                self.assertLessEqual(abs(w["audio"]["game_ticks_us"] - w["audio"]["span_us"]), gate_runs.PACE_AUDIO_BUFFER_US + 2 * gate_runs.PACE_TICK_US)

    def test_runs_at_different_costs_agree_within_a_tick(self):
        windows = [self.window(pace_log(33_333)), self.window(pace_log(73_000, burn=40))]
        self.assertTrue(gate_logs.pace_runs_agree(windows, tick_travel=windows[0]["path_per_tick"]))
        self.assertEqual(windows[1]["burn_ms"], 40)
        self.assertTrue(any(k >= 2 for k in windows[1]["ticks_per_frame"]))

    def test_the_legacy_loop_is_caught(self):
        # one tick per frame: a 73 ms frame costs 2.2 ticks of the clock, so game time lags
        slow = self.window(pace_log(73_000, legacy=True, burn=40))
        self.assertFalse(slow["ticks_follow_clock"])
        base = self.window(pace_log(33_333))
        self.assertFalse(gate_logs.pace_runs_agree([base, slow], tick_travel=base["path_per_tick"]))

    def test_audio_off_the_clock_is_caught_and_silent_music_is_not_a_failure(self):
        self.assertFalse(self.window(pace_log(40_000, audio_scale=0.9))["audio"]["mixer_follows_clock"])
        self.assertFalse(self.window(pace_log(40_000, audio_scale=0.9))["audio"]["music_follows_clock"])
        self.assertIsNone(self.window(pace_log(40_000, music=False))["audio"]["music_follows_clock"])

    def test_a_run_that_never_moved_or_ends_early_has_no_window(self):
        self.assertIsNone(self.window(pace_log(40_000, speed=0)))
        self.assertIsNone(self.window(pace_log(40_000, seconds=6)))
        self.assertIsNone(self.window(""))

    def test_checks_pass_for_a_good_run_and_name_the_failure_for_the_legacy_one(self):
        good = pace_log(33_333) + pace_log(73_000, burn=40)
        # two runs in one log: the offsets pick each window
        first, second = pace_log(33_333), pace_log(73_000, burn=40)
        text = first + second
        marks = [{"burn_ms": 0, "begin": 0, "end": len(first)}, {"burn_ms": 40, "begin": len(first), "end": len(text)}]
        checks, failure, evidence = gate_runs.pace_checks(text, marks, (0, 40))
        self.assertTrue(all(checks.values()), {k: v for k, v in checks.items() if not v})
        self.assertEqual(len(evidence["windows"]), 2)
        bad = first + pace_log(73_000, legacy=True, burn=40)
        marks[1]["end"] = len(bad)
        checks, _, _ = gate_runs.pace_checks(bad, marks, (0, 40))
        self.assertFalse(checks["burn_40ms_ticks_follow_clock"])
        self.assertFalse(checks["same_distance_within_a_tick"])
        self.assertIsNotNone(good)

    def test_missing_run_fails(self):
        checks, _, _ = gate_runs.pace_checks("", [], (0, 40))
        self.assertFalse(all(checks.values()))

class RunIdentity(unittest.TestCase):
    def controls(self, run):
        for identity, reason in (
            (IDENTITY + PACK_LINE, None),
            (IDENTITY.replace(BUILD, "000000000000"), "guest build id mismatch"),
            (PACK_LINE, "missing guest build id"),
            (IDENTITY.replace(CONFIG, "0" * 64), "guest configuration digest mismatch"),
            (PACK_LINE + BUILD_LINE.rstrip("\n"), "incomplete guest build identity record"),
            (IDENTITY + BUILD_LINE.removesuffix("e\n"), "multiple guest build ids"),
            (BUILD_LINE + PACK_LINE.rstrip("\n"), "incomplete guest pack SHA-256 record"),
            (IDENTITY + BUILD_LINE, "multiple guest build ids"),
            (BUILD_LINE, "missing guest pack SHA-256"),
            (IDENTITY + PACK_LINE.replace(PACK, "0" * 64) + PACK_LINE, "guest pack SHA-256 mismatch"),
        ):
            with self.subTest(reason=reason):
                report = run(identity)
                self.assertTrue(all(report["checks"].values()), report)
                self.assertEqual(report["pass"], reason is None, report)
                if reason:
                    self.assertIn(reason, report["identity_failure"])
                else:
                    self.assertIsNone(report["identity_failure"])

    def test_power_cycle_requires_its_own_guest_identity(self):
        saved = IDENTITY + lara("saved", 81)
        restored = ("save: bind status=0 slots=1 bytes=32768\nplay: ready=title hz=30\n" + READY
                    + lara("restored", 3) + lara("tick", 50, (1500, 500, 2000)))

        def run(identity):
            with mock.patch.object(gate_runs, "Drive") as drive:
                drive.return_value.text.return_value = identity + restored
                return gate_runs.power_cycle(None, "play.z64", None, expected=EXPECTED,
                                             sram=None, saved_text=saved, ares=None, timeout=1)
        self.controls(run)

    def test_death_end_requires_guest_identity(self):
        def run(identity):
            text, marks = DeathEndChecks().run_log()
            checks = gate_runs.death_end_checks(text, marks)
            with mock.patch.object(gate_runs, "Drive") as drive, \
                 mock.patch.object(gate_runs, "death_end_checks", return_value=checks):
                drive.return_value.text.return_value = identity + text.removeprefix(IDENTITY)
                return gate_runs.death_end(None, "gate.z64", None, expected=EXPECTED,
                                          sram=None, ares=None, timeout=1)
        self.controls(run)

    def test_pace_requires_guest_identity(self):
        def run(identity):
            log = pace_log(73_000, burn=40, seconds=17)
            marks = [{"burn_ms": 40, "begin": 0, "end": len(log)}]
            parsed = gate_runs.pace_checks(log, marks, (40,))
            with mock.patch.object(gate_runs, "Drive") as drive, \
                 mock.patch.object(gate_runs, "pace_checks", return_value=parsed):
                drive.return_value.text.return_value = identity + log
                drive.return_value.wait.return_value = len(identity)
                return gate_runs.pace_run(None, "gate.z64", None, expected=EXPECTED,
                                          sram=None, ares=None, timeout=1, burns=(40,))
        self.controls(run)

    def test_overlay_requires_guest_identity(self):
        def run(identity):
            with tempfile.TemporaryDirectory() as tmp, mock.patch.object(perf_overlay, "Drive") as drive, \
                 mock.patch.object(perf_overlay, "expected_identity", return_value=EXPECTED), \
                 mock.patch.object(perf_overlay, "last_picture", return_value=Path(tmp)), \
                 mock.patch.object(perf_overlay, "to_png"), mock.patch("builtins.print"), \
                 mock.patch.object(sys, "argv", ["perf_overlay.py", "--rom", "play.z64", "--pack", "pack",
                                                 "--out", tmp, "--windows", "1"]):
                shown = "perf: cart frames=1 glyphs=2\n"
                final = shown + "game-render: plain\n"
                drive.return_value.text.side_effect = ["", "", shown, shown, final, final + identity]
                drive.return_value.wait.return_value = len(shown)
                with self.assertRaises(SystemExit) as raised:
                    perf_overlay.main()
                summary = json.loads((Path(tmp) / "summary.json").read_text())
                self.assertEqual(raised.exception.code, 0 if summary["pass"] else 1)
                return summary
        self.controls(run)



class Delivery(unittest.TestCase):
    GOOD = dict(rom_md5="aa", build="B1", config="C1", gate_build="B2", gate_config="C2",
                pack_sha="cc", gate_rom_md5="dd", pack_videos=["CAFE"])
    SUMMARY = {"pass": True, "reload_exercised": True, "sound_exercised": True, "sound": {"pass": True},
               "cutscene": {"skip_logged": True}, "rom_md5": "aa", "build_id": "B1", "config_id": "C1", "pack_sha256": "cc",
               "video_gate": True, "pack_videos": ["CAFE"],
               "save_load": {"pass": True, "build_id": "B1", "config_id": "C1", "pack_sha256": "cc"},
               "death_end": {"pass": True, "rom_md5": "dd", "build_id": "B2", "config_id": "C2", "pack_sha256": "cc"},
               "pace": {"pass": True, "rom_md5": "dd", "build_id": "B2", "config_id": "C2", "pack_sha256": "cc",
                        "marks": [{"burn_ms": 0}, {"burn_ms": 40}]}}

    def test_complete_summary_is_deliverable(self):
        self.assertEqual(deliver_check.failures(self.SUMMARY, **self.GOOD), [])

    def test_each_missing_part_refuses(self):
        import copy
        for path, value in ((("pass",), False), (("save_load",), None), (("save_load", "pass"), False),
                            (("save_load", "build_id"), "x"), (("save_load", "pack_sha256"), "x"),
                            (("death_end",), None), (("death_end", "pass"), False), (("death_end", "rom_md5"), "x"),
                            (("death_end", "build_id"), "x"), (("death_end", "pack_sha256"), "x"),
                            (("pace",), None), (("pace", "pass"), False), (("pace", "rom_md5"), "x"),
                            (("pace", "build_id"), "x"), (("pace", "pack_sha256"), "x"), (("pace", "marks"), [{"burn_ms": 0}]),
                            (("rom_md5",), "x"), (("build_id",), "x"), (("pack_sha256",), "x"),
                            (("config_id",), "x"), (("save_load", "config_id"), "x"),
                            (("death_end", "config_id"), "x"), (("pace", "config_id"), "x"),
                            (("video_gate",), False), (("pack_videos",), []), (("cutscene",), None)):
            with self.subTest(path=path):
                summary = copy.deepcopy(self.SUMMARY)
                target = summary
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                self.assertTrue(deliver_check.failures(summary, **self.GOOD))

    def test_video_verdict_is_required_only_when_the_pack_has_videos(self):
        summary = dict(self.SUMMARY, video_gate=False, pack_videos=None)
        self.assertEqual(deliver_check.failures(summary, **dict(self.GOOD, pack_videos=set())), [])
        self.assertTrue(deliver_check.failures(summary, **self.GOOD))

    def test_pack_video_names_reads_the_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "p.pack")
            path.write_bytes(b"TR6P" + b"\0" * 100 + b"VIDEO_CAFE\0FILM_CAFE\0VIDEO_ESCAPE" + b"\0" * 70000 + b"VIDEO_SNOW")
            self.assertEqual(pack_video_names(path), {"CAFE", "ESCAPE", "SNOW"})
            path.write_bytes(b"TR6P" + b"\0" * 100 + b"FILM_CAFE")
            self.assertEqual(pack_video_names(path), set())


class CursorStep(unittest.TestCase):
    """The decision of the cursor step against synthetic logs: the last logged cursor is the cursor."""
    OPEN = "ui: picker open load=0 valid=000 unusable=000 index=0\n"

    def index(self, *values):
        return "".join(f"ui: picker index={v}\n" for v in values)

    def test_one_press_lands_on_the_slot(self):
        self.assertEqual(cursor_step(self.OPEN + self.index(2), SAVE_SLOT, 0, 3, 1), ("done", 1))

    def test_the_open_line_is_not_a_cursor_move(self):
        self.assertEqual(cursor_step(self.OPEN, SAVE_SLOT, 0, 3, 1), ("wait", 0))

    def test_lost_press_is_pressed_again_after_the_wait_and_not_before(self):
        self.assertEqual(cursor_step(self.OPEN, SAVE_SLOT, 0, CURSOR_RETRY_FRAMES - 1, 1), ("wait", 0))
        self.assertEqual(cursor_step(self.OPEN, SAVE_SLOT, 0, CURSOR_RETRY_FRAMES, 1), ("again", 0))
        # the second press arrives: done
        self.assertEqual(cursor_step(self.OPEN + self.index(2), SAVE_SLOT, 0, 3, 2), ("done", 1))

    def test_doubled_press_overshoots_and_is_pressed_again_until_on_the_slot(self):
        log = self.OPEN + self.index(2, 1)            # one press seen twice: slot 3, then slot 2
        self.assertEqual(cursor_step(log, SAVE_SLOT, 0, 3, 1), ("again", 2))
        log += self.index(0)                          # the next press: slot 1; answered lines are not new
        self.assertEqual(cursor_step(log, SAVE_SLOT, 2, 3, 2), ("again", 3))
        log += self.index(2)                          # and the next lands
        self.assertEqual(cursor_step(log, SAVE_SLOT, 3, 3, 3), ("done", 4))

    def test_wrong_cursor_with_no_new_line_waits_for_the_press_in_flight(self):
        log = self.OPEN + self.index(2, 1)
        self.assertEqual(cursor_step(log, SAVE_SLOT, 2, 3, 2), ("wait", 2))

    def test_gives_up_after_the_bound_by_name(self):
        self.assertEqual(cursor_step(self.OPEN + self.index(1), SAVE_SLOT, 0, 3, MAX_PRESSES)[0], "fail")
        self.assertEqual(cursor_step(self.OPEN, SAVE_SLOT, 0, CURSOR_RETRY_FRAMES, MAX_PRESSES)[0], "fail")

    def test_every_other_step_has_an_acknowledgement_in_the_log(self):
        self.assertEqual([step.label for step in SAVE_STEPS if step.repeat], ["Up: cursor to slot 3"])
        self.assertTrue(all(step.ack for step in SAVE_STEPS))


class GateLoopOnSyntheticCart(unittest.TestCase):
    """boot.py's loop, unmodified, against tools/ship/synthetic_cart.py in place of test/run/ares.sh."""
    SHIP = Path(__file__).resolve().parent

    def run_gate(self, fault, save=True, gate=False, sound=False, expected_exit=None):
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as tmp:
            root = Path(tmp) / "root"
            (root / "tools/ship").mkdir(parents=True)
            (root / "test/run").mkdir(parents=True)
            for source in self.SHIP.glob("*.py"):
                shutil.copy(source, root / "tools/ship" / source.name)
            (root / "test/run/ares.sh").write_text('#!/usr/bin/env bash\necho 0 >"$1/slot.txt"\n'
                                                  'exec python3 "$SIM" "$@"\n')
            shipped, ares, out = Path(tmp) / "build", Path(tmp) / "ares", Path(tmp) / "out"
            (shipped / "generated").mkdir(parents=True)
            header = shipped / "generated/cart_identity.h"
            header.write_text(f'#define TR64_BUILD_ID "{BUILD}"\n#define TR64_CONFIG_ID "{CONFIG}"\n')
            rom, pack = Path(tmp) / "relocated.z64", Path(tmp) / "pack"
            pack.write_bytes(b"pack")
            shutil.copy(self.SHIP.parents[1] / "test/run/guest_identity.py", root / "test/run")
            rom.write_bytes(b"rom")
            ares.write_bytes(b"")
            gate_args = []
            if gate:
                gate_rom, gate_header = Path(tmp) / "gate.z64", Path(tmp) / "gate-identity.h"
                gate_rom.write_bytes(b"gate ROM")
                gate_header.write_text(f'#define TR64_BUILD_ID "{GATE_BUILD}"\n#define TR64_CONFIG_ID "{GATE_CONFIG}"\n')
                gate_args = ["--gate-rom", str(gate_rom), "--gate-identity-header", str(gate_header), "--pace-burn", "0,40"]
            env = dict(os.environ, SIM=str(root / "tools/ship/synthetic_cart.py"), SIM_FAULT=fault)
            result = subprocess.run([sys.executable, str(root / "tools/ship/boot.py"), "--rom", str(rom), "--out", str(out),
                                     "--pack", str(pack), "--identity-header", str(header),
                                     *(["--save-load"] if save else []), *gate_args,
                                     *(["--reload", "--sound"] if sound else []),
                                     "--ares", str(ares), "--timeout", "60", "--secondary-timeout", "20"],
                                    env=env, capture_output=True, text=True, timeout=240)
            self.assertTrue((out / "summary.json").exists(), result.stderr)
            summary = json.loads((out / "summary.json").read_text())
            if expected_exit is not None:
                self.assertEqual(result.returncode, expected_exit, result.stderr)
                if expected_exit == 3:
                    self.assertIn(f"INVALID RUN: {summary['invalid_run']}", result.stderr)
            elif not save:
                self.assertEqual(result.returncode, 0 if summary["pass"] else 1, result.stderr)
            return summary, (out / "ares.log").read_bytes().decode("utf-8", "surrogateescape"), (out / "input.log").read_text()

    def test_shortest_base_only_gate_requires_guest_identity(self):
        for fault, reason in (("", None), ("wrong-id", "guest build id mismatch"),
                              ("missing-id", "missing guest build id"),
                              ("wrong-config", "guest configuration digest mismatch"),
                              ("wrong-pack", "guest pack SHA-256 mismatch"),
                              ("missing-pack", "missing guest pack SHA-256")):
            with self.subTest(fault=fault):
                summary, _, _ = self.run_gate(fault, save=False)
                self.assertTrue(summary["forward_displaced"] and summary["ring_opened_closed"])
                self.assertEqual(summary["pass"], reason is None, summary)
                if reason:
                    self.assertIn(reason, summary["identity_failure"])
                else:
                    self.assertIsNone(summary["identity_failure"])

    def test_terminal_report_is_required_and_counts_underruns(self):
        for fault, terminal, expected in (
                ("", "level1", [0, 0, 0, 0]),
                ("terminal-underrun", "level1", [0, 0, 0, 1]),
                ("terminal-missing", None, [0, 0, 0]),
                ("terminal-no-counter", None, [0, 0, 0]),
                ("terminal-bad-value", None, [0, 0, 0]),
                ("terminal-repeat-key", None, [0, 0, 0])):
            with self.subTest(fault=fault):
                summary, log, inputs = self.run_gate(fault, save=False, sound=True, expected_exit=(
                    0 if not fault else 1 if terminal else 3))
                self.assertIn("L+Start: sample terminal audio", inputs)
                before = log[:log.rindex("play-select: from=level1 to=level1 command=1")]
                self.assertTrue(sound_evidence(before, 3)["pass"])
                self.assertEqual(summary["sound"]["terminal_level"], terminal)
                self.assertEqual([r.get("underruns") for r in summary["sound"]["output_reports"]], expected)
                self.assertEqual(summary["sound"]["pass"], not fault, summary)
                self.assertEqual(summary["pass"], not fault, summary)

    def test_copied_previous_output_row_is_not_a_fresh_sample(self):
        summary, log, _ = self.run_gate("copied-output", save=False, sound=True, expected_exit=3)
        rows = [row for row in log.split("\n") if row.startswith("audio-output: cart ")]
        self.assertEqual(rows[-1], rows[-2])
        self.assertIn("audio-output counters did not advance", summary["invalid_run"])

    def test_empty_music_row_invalidates_the_packet(self):
        summary, log, _ = self.run_gate("music-empty", save=False, sound=True, expected_exit=3)
        self.assertIn("\nmusic: \naudio: ", log)
        self.assertIn("malformed music: row", summary["invalid_run"])

    def test_music_underrun_fails_even_when_effects_and_output_are_clean(self):
        summary, _, _ = self.run_gate("music-underrun", save=False, sound=True, expected_exit=1)
        sound = summary["sound"]
        self.assertIsNone(summary.get("invalid_run"))
        self.assertFalse(sound["pass"])
        self.assertEqual(sound["terminal_level"], "level1")
        self.assertEqual([row["underruns"] for row in sound["music_reports"]], [0, 0, 0, 1])
        self.assertTrue(all(row["underruns"] == 0 for row in sound["reports"] + sound["output_reports"]))

    def test_shutdown_copy_back_cannot_hide_a_second_terminal_report(self):
        summary, log, _ = self.run_gate("terminal-duplicate", save=False, sound=True, expected_exit=3)
        self.assertFalse(summary["pass"])
        start = log.rindex("play-select: from=level1 to=level1 command=1")
        self.assertTrue(sound_evidence(log[:start], 3)["pass"])
        self.assertEqual([row["underruns"] for row in summary["sound"]["output_reports"]], [0, 0, 0, 0])
        self.assertIn("extra audio row after terminal packet", summary["invalid_run"])
        self.assertEqual(summary["launcher_exit"], 143)
        self.assertIsNone(summary["sound"]["terminal_level"])
        self.assertFalse(summary["sound"]["pass"])

    def test_copy_back_failure_cannot_pass_with_a_good_streamed_log(self):
        summary, _, _ = self.run_gate("finalization-failed", save=False, sound=True)
        self.assertFalse(summary["pass"])
        self.assertTrue(summary["sound"]["pass"])
        self.assertEqual(summary["sound"]["terminal_level"], "level1")
        self.assertEqual(summary["launcher_exit"], 255)

    def test_interleaved_guest_row_makes_the_run_invalid_not_an_underrun(self):
        summary, log, _ = self.run_gate("sound-interleaved", save=False, sound=True, expected_exit=3)
        self.assertFalse(summary["pass"])
        self.assertFalse(summary["sound"]["pass"])
        number, row = next((n, line) for n, line in enumerate(log.splitlines(), 1)
                           if line.startswith("audio-output: cart") and "[ares]" in line)
        reason = f"line {number}: guest report interleaved with emulator output: rerun: {row}"
        self.assertEqual(summary.get("invalid_run"), reason)
        self.assertEqual(summary["sound"]["invalid_reason"], reason)
        self.assertIsNone(summary["sound"]["terminal_level"])
        self.assertEqual([r["underruns"] for r in summary["sound"]["output_reports"]], [0])

    def test_emulator_first_row_before_expected_packet_is_invalid(self):
        summary, log, _ = self.run_gate("sound-prefix-ares", save=False, sound=True, expected_exit=3)
        self.assertFalse(summary["sound"]["pass"])
        number, row = next((n, line) for n, line in enumerate(log.split("\n"), 1)
                           if line.startswith("[ares]") and "audio-output: cart" in line)
        self.assertIn("underruns=1", row)
        self.assertEqual(summary["invalid_run"],
                         f"line {number}: guest report interleaved with emulator output: rerun: {row}")

    def test_opaque_shutdown_bytes_are_outside_the_sample(self):
        summary, log, _ = self.run_gate("terminal-prefix-harness", save=False, sound=True, expected_exit=0)
        self.assertIn("TESTHARNESS notice audio-output: cart", log)
        self.assertEqual(summary["sound"]["terminal_level"], "level1")
        self.assertTrue(summary["sound"]["pass"])

    def test_standalone_screenshot_does_not_break_terminal_packet(self):
        summary, log, _ = self.run_gate("standalone-screenshot", save=False, sound=True, expected_exit=0)
        self.assertIn("[ares] screenshot test.ppm (320x240 bpp2 fb=0x3a5000 tag=t0144)\naudio-output: cart", log)
        self.assertTrue(summary["sound"]["pass"])

    def test_split_prefix_cannot_hide_an_earlier_underrun(self):
        summary, log, _ = self.run_gate("earlier-split", save=False, sound=True, expected_exit=3)
        self.assertIn("audio-out[ares] screenshot test.ppm\nput:", log)
        self.assertIn("guest report interleaved with emulator output", summary["invalid_run"])

    def test_split_duplicate_inside_terminal_packet_is_invalid(self):
        summary, log, _ = self.run_gate("terminal-duplicate-split", save=False, sound=True, expected_exit=3)
        self.assertIn("aud[ares] screenshot test.ppm\nio:", log)
        self.assertIn("guest report interleaved with emulator output", summary["invalid_run"])

    def test_non_utf8_inside_prefix_is_invalid(self):
        summary, log, _ = self.run_gate("earlier-non-utf8", save=False, sound=True, expected_exit=3)
        self.assertIn("audio-out\udcffput:", log)
        self.assertIn(r"audio-out\udcffput:", summary["invalid_run"])

    def test_bare_cr_is_not_a_terminal_record_boundary(self):
        summary, log, _ = self.run_gate("terminal-bare-cr", save=False, sound=True, expected_exit=3)
        self.assertIn("\raudio-time:", log)
        self.assertIn(r"\raudio-time:", summary["invalid_run"])

    def test_dispatches_base_and_gate_identity_independently(self):
        summary, _, _ = self.run_gate("", gate=True)
        for name, expected in (("save_load", (BUILD, CONFIG)), ("death_end", (GATE_BUILD, GATE_CONFIG)),
                               ("pace", (GATE_BUILD, GATE_CONFIG))):
            report = summary[name]
            self.assertIsNone(report["identity_failure"], report)
            self.assertEqual((report["build_id"], report["config_id"]), expected)

    def test_ship_check_rejects_a_stale_base_and_its_matching_sidecar_with_a_fresh_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ship, runner, engine, bin_ = (root / p for p in ("tools/ship", "test/run", "engine/in", "bin"))
            for directory in (ship, runner, engine, bin_):
                directory.mkdir(parents=True)
            for name in ("ship.sh", "boot.py", "gate_runs.py", "gate_logs.py", "drive.py", "deliver_check.py"):
                shutil.copy2(self.SHIP / name, ship)
            repo = self.SHIP.parents[1]
            shutil.copyfile(repo / "test/run/guest_identity.py", runner / "guest_identity.py")
            shutil.copyfile(repo / "engine/in/play.c", engine / "play.c")
            (ship / "gate_place.py").write_text("# fixture Makefile doesn't consume player data\n")
            rtk = bin_ / "rtk"
            rtk.write_text('#!/bin/sh\ncase "$1" in proxy) shift;; esac\n'
                           'if [ "$1" = git ]; then\n'
                           '  case "$*" in *"status --porcelain") exit 0;; esac\n'
                           '  printf "%s\\n" "$FIXTURE_ROOT"\n'
                           'else exec "$@"; fi\n')
            rtk.chmod(0o755)
            (root / "Makefile").write_text(
                "mode = $(if $(TR64_SCENE_TEST_SOURCES),gate,base)\n"
                "cart-identity:\n"
                '\tmkdir -p "$(BUILD_DIR)/generated"\n'
                '\tcp "$(CURDIR)/$(mode).h" "$(BUILD_DIR)/generated/cart_identity.h"\n'
                "rom: cart-identity\n"
                '\tcp "$(CURDIR)/$(mode).h" "$(BUILD_DIR)/tr64.z64"\n')
            (runner / "ares.sh").write_text(
                '#!/bin/sh\n'
                '''awk -F '"' '/TR64_BUILD_ID/{build=$2} /TR64_CONFIG_ID/{config=$2}
END{print "play: build_id=" build " config_id=" config " mode=live"}' "$2" >"$3"\n'''
                f'printf "level-pack: sha256={PACK}\\n" >>"$3"\n')

            def header(source, config):
                return f'#define TR64_BUILD_ID "{source}"\n#define TR64_CONFIG_ID "{config}"\n'

            (root / "base.h").write_text(header(BUILD, CONFIG))
            gate = header(GATE_BUILD, GATE_CONFIG)
            (root / "gate.h").write_text(gate)
            (root / "pack").write_bytes(b"pack")
            (root / "ares").touch()
            for source, config, reason in ((BUILD, CONFIG, None),
                                           ("111111111111", CONFIG, "guest build id mismatch"),
                                           (BUILD, "0" * 64, "guest configuration digest mismatch")):
                with self.subTest(reason=reason):
                    build = root / "build"
                    (build / "generated").mkdir(parents=True)
                    base = header(source, config)
                    (build / "generated/cart_identity.h").write_text(base)
                    (build / "tr64.z64").write_text(base)
                    env = dict(os.environ, PATH=f"{bin_}:{os.environ['PATH']}", FIXTURE_ROOT=str(root),
                               TMPDIR=str(root / "tmp"), BUILD_DIR=str(build), TR64_GATE_DIR=str(build / "gate"),
                               TR64_LEVEL_PACK=str(root / "pack"), ARES_BINARY=str(root / "ares"), TR64_PACE_BURN="0,40")
                    result = subprocess.run([ship / "ship.sh", "check"], env=env, capture_output=True, text=True, timeout=20)
                    # This is the real boot.py verifier; the transport fixture only emits boot evidence.
                    self.assertEqual(result.returncode, 1, result.stderr)
                    summary = json.loads((build / "play-check/summary.json").read_text())
                    failure = summary["identity_failure"]
                    if reason is None:
                        self.assertIsNone(failure, summary)
                    else:
                        self.assertIn(reason, failure, summary)
                    self.assertEqual((summary["build_id"], summary["config_id"]), (source, config))
                    self.assertEqual((build / "gate/tr64.z64").read_text(), gate)
                    shutil.rmtree(build)

            # Use the real verifier and shell commands. The transport emits a
            # corrupted guest row; no game/ROM or network delivery is needed.
            (runner / "ares.sh").write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' 'play: ready=level1 hz=30' "
                "'TESTHARNESS notice audio-output: cart underruns=1' > \"$3\"\n")
            build.mkdir()
            (build / "tr64.z64").write_text(header(BUILD, CONFIG))
            env["TR64_VISUAL_REVIEWED"] = "1"
            message = "INVALID RUN: line 2: guest report interleaved with emulator output: rerun:"
            for command in ("check", "deliver"):
                with self.subTest(command=command):
                    result = subprocess.run([ship / "ship.sh", command], env=env, capture_output=True,
                                            text=True, timeout=20)
                    self.assertEqual(result.returncode, 3, result)
                    self.assertIn(message, result.stderr)
                    self.assertNotIn("sound not verified", result.stderr)

    def test_smoke_does_not_press_for_an_ended_film_and_separates_release(self):
        began, pressed, released = {}, set(), set()
        escape = "video: begin name=ESCAPE \n"
        core = "video: begin name=CORE \n"
        def scan(text, now):
            return list(smoke_film_input(text, now, began, pressed, released))
        self.assertEqual(scan(escape + "video: name=ESCAPE result=done \n", 0), [])
        self.assertEqual(scan(core, 1), [])
        self.assertEqual(scan(core, 2), [("Start: skip CORE", "10000000 0 0\n")])
        more = core + "video: name=CORE result=skipped \nvideo: begin name=CAFE \n"
        self.assertEqual(scan(more, 3), [("release CORE", "0 0 0\n")])
        self.assertEqual(scan(more, 3.1), [])

    def presses(self, text, label):
        return [line for line in text.splitlines() if f" {label} pad=08000000" in line]

    def test_no_fault(self):
        summary, log, inputs = self.run_gate("")
        base = summary["save_load"]["base_run"]
        self.assertIsNone(summary["stalled"])
        self.assertTrue(base["picker_accepted_slot_3"] and base["saved_to_slot_3"], summary["save_load"])
        self.assertEqual(len(self.presses(inputs, "Up: cursor to slot 3")), 1)
        self.assertTrue(summary["forward_displaced"])

    def test_lost_up_press_is_pressed_again(self):
        summary, log, inputs = self.run_gate("lost-up")
        base = summary["save_load"]["base_run"]
        self.assertIsNone(summary["stalled"])
        self.assertTrue(base["picker_accepted_slot_3"] and base["saved_to_slot_3"], summary["save_load"])
        self.assertEqual(len(self.presses(inputs, "Up: cursor to slot 3")), 1)
        self.assertEqual(len(self.presses(inputs, "Up: cursor to slot 3 (again)")), 1)
        self.assertNotIn("accept slot=0", log)

    def test_doubled_up_press_is_corrected(self):
        summary, log, inputs = self.run_gate("doubled-up")
        base = summary["save_load"]["base_run"]
        self.assertIsNone(summary["stalled"])
        self.assertTrue(base["picker_accepted_slot_3"] and base["saved_to_slot_3"], summary["save_load"])
        # slot 3 then slot 2 from one press: two more presses (slot 1, slot 3)
        self.assertEqual(len(self.presses(inputs, "Up: cursor to slot 3 (again)")), 2)
        self.assertIn("ui: picker accept slot=2 load=0", log)
        self.assertNotIn("accept slot=0", log)
        self.assertNotIn("accept slot=1", log)

    def test_lost_press_of_another_step_fails_fast_by_name(self):
        summary, log, inputs = self.run_gate("lost-b-picker")
        self.assertIn("B: open the slot picker", summary["stalled"])
        self.assertFalse(summary["pass"])
        self.assertNotIn("picker accept", log)
        self.assertLess(log.count("play-frame:"), 2500)  # the stall rule, not the run's deadline

    def test_accept_into_the_wrong_slot_fails_at_once_by_name(self):
        summary, log, inputs = self.run_gate("cursor-slips")
        self.assertIn("B: accept slot 3", summary["stalled"])
        self.assertIn("slot=0", summary["stalled"])
        self.assertFalse(summary["save_load"]["base_run"]["picker_accepted_slot_3"])


if __name__ == "__main__":
    unittest.main()
