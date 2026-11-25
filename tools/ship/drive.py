# SPDX-License-Identifier: MIT
"""One ares process on a shipping ROM, driven through its pad file and read through its log.

The shared harness (test/run/ares.sh: private Xvfb, slot locks, deadlines) is the only way
to start the emulator. Steps wait on the cart's own log lines and on game ticks, never on
wall time alone: the emulator may run at a small fraction of real time on a loaded host.
"""
import os
from pathlib import Path
import re
import signal
import subprocess
import time

# Pad words as ares' test input wants them (the high half of the N64 button word).
PAD = dict(A=0x80000000, B=0x40000000, Z=0x20000000, START=0x10000000, UP=0x08000000,
           DOWN=0x04000000, LEFT=0x02000000, RIGHT=0x01000000, L=0x00200000, R=0x00100000,
           CUP=0x00080000, CDOWN=0x00040000, CLEFT=0x00020000, CRIGHT=0x00010000)
QUEUE_LIMIT = 4 * 3600  # seconds a run may wait for one of the two host-wide ares slots
REMOTE_STOP_LIMIT = 900  # seconds a stopped run on TR64_ARES_HOST may take to stop ares there and copy its results back
SILENCE_LIMIT = 900     # seconds the emulator's log may stay unchanged once it runs
FORWARD = "0 0 80\n"  # stick fully forward
IDLE = "0 0 0\n"
SAMPLED_EFFECT_LIMIT = 60  # host seconds after observing the press's controller reply


def pad_journal(pad):
    """The append-only record of every value written to `pad` (the ARES_TEST_INPUT file)."""
    pad = Path(pad)
    return pad.with_name(pad.name + ".seq")


def pad_start(pad):
    """Create the pad file (idle) and its journal. The remote harness (test/run/ares-remote.sh) forwards the
    journal, line by line in order, because the file keeps only the latest value: a value replaced before the
    emulator reads it is lost, and a press replaced by its release is a press that never happened."""
    pad = Path(pad)
    pad.write_text(IDLE)
    pad_journal(pad).write_text("")


def pad_write(pad, value):
    """Set the pad: the file ares reads (replaced atomically), then the journal."""
    pad = Path(pad)
    temp = pad.with_suffix(".tmp")
    temp.write_text(value)
    temp.replace(pad)
    with pad_journal(pad).open("a") as journal:
        journal.write(value)


def word(*names):
    return f"{sum(PAD[n] for n in names):08x} 0 0\n"


def stop_grace(local):
    """Seconds to wait for a TERMed run to finish: `local` here, much more when it runs on TR64_ARES_HOST
    (stop ares there, export the SRAM, copy log, pictures and files back; the wait ends when the runner
    exits, so the bound only guards a hang)."""
    host = os.environ.get("TR64_ARES_HOST", "")
    return local if host in ("", "local", "localhost") else max(local, REMOTE_STOP_LIMIT)


class ShotPruner:
    """ares writes a picture about every frame; keep the disk bounded.

    Kept: the first two after each level start, every fifth otherwise, every
    third while a cutscene plays. The rest are deleted as the log shows them."""
    def __init__(self):
        self.pos, self.count, self.since, self.cut = 0, 0, 0, False

    def run(self, text):
        end = text.rfind("\n") + 1
        for line in text[self.pos:end].splitlines():
            ready = re.match(r"play: ready=(\w+) hz=", line)
            if ready:
                self.cut, self.since = ready[1].startswith("cut"), 0
                continue
            shot = re.match(r"\[ares\] screenshot (\S+\.ppm) ", line)
            if shot:
                self.count += 1
                self.since += 1
                if self.since > 2 and self.count % (3 if self.cut else 5):
                    Path(shot[1]).unlink(missing_ok=True)
        self.pos = end


class GateFailure(Exception):
    """A step of a gate run did not happen; the message names it."""


class SampledEffectFailure(GateFailure):
    exit_status = 68


class SampledEffect:
    """A timely controller reply is necessary, but not the guest action."""

    def __init__(self, label, port, entry):
        self.label, self.port, self.entry = label, port, entry
        self.deadline = None

    def observed(self, samples, complete, failed=None):
        now = time.monotonic()
        if self.deadline is None and sampled(samples, self.port, self.entry):
            self.deadline = now + SAMPLED_EFFECT_LIMIT
        if self.deadline is not None and (failed or now >= self.deadline):
            reason = failed or f"guest effect not observed within {SAMPLED_EFFECT_LIMIT} s of sample"
            raise SampledEffectFailure(f"pad-no-effect port={self.port} entry={self.entry} action={self.label}: {reason}")
        return self.deadline is not None and complete


class Drive:
    def __init__(self, root, rom, out, *, timeout, ares=None, sram_in=None, sram_out=None, two_pads=False):
        self.out, self.rom, self.timeout = Path(out), Path(rom), timeout
        self.out.mkdir(parents=True)
        self.log, self.pad, self.vi = self.out / "ares.log", self.out / "pad.in", self.out / "vi"
        self.vi.mkdir()
        pad_start(self.pad)
        env = os.environ.copy()
        env.update(ARES_EXPANSION_PAK="false", ARES_TEST_INPUT=str(self.pad),
                   ARES_HOST_SCREENSHOT="1", ARES_HOST_SHOT_EVERY_MS="1000",
                   ARES_HOST_SHOT_DIR=str(self.vi), ARES_HOST_SHOT_MAX="20000",
                   TR64_SHOT_PRUNE="1")
        self.pad2 = None
        if two_pads:
            self.pad2 = self.out / "pad2.in"
            self.pad2.write_text(IDLE)
            self.pad_entries = 2
            pad_journal(self.pad).write_text(f"pads-v2\n1 {IDLE}2 {IDLE}")
            env.update(ARES_TEST_INPUT_2=str(self.pad2), TR64_SHOT_PRUNE="")
        else:
            env.pop("ARES_TEST_INPUT_2", None)
        if ares:
            env["ARES_BINARY"] = str(ares)
        if sram_in:
            env["ARES_SRAM_IN"] = str(sram_in)
        if sram_out:
            env["ARES_SRAM_OUT"] = str(sram_out)
        # RTK's wrapper does not forward TERM: own the real harness process so its trap
        # completes child/slot cleanup before the final log is consumed.
        self.proc = subprocess.Popen(["bash", str(Path(root) / "test/run/ares.sh"), str(self.out),
                                      str(self.rom), str(self.log), str(timeout)],
                                     env=env, start_new_session=True)
        # The deadline runs from the moment a slot is taken (ares.sh writes slot.txt then); the wait for
        # a free slot on a busy host has its own, long, limit.
        self.queued_until, self.deadline = time.monotonic() + QUEUE_LIMIT, None
        self.pruner = ShotPruner()
        self.servicers = []
        self.log_size, self.last_progress = -1, time.monotonic()

    # -- reading
    def text(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def tick(self, text=None):
        found = re.findall(r"^play-frame: tick=(\d+)", self.text() if text is None else text, re.M)
        return int(found[-1]) if found else 0

    # -- the pad
    def inject(self, value, label, *, pad=1):
        path = self.pad if pad == 1 else self.pad2 if pad == 2 else None
        if path is None:
            raise ValueError(f"pad {pad} not enabled")
        if self.pad2 is None:
            pad_write(path, value)
        else:
            with pad_journal(self.pad).open("a") as journal:
                journal.write(f"{pad} {value}")
        with (self.out / "input.log").open("a") as stream:
            name = "pad" if pad == 1 else "pad2"
            stream.write(f"after_tick={self.tick()} {label} {name}={value}")
        if self.pad2 is not None:
            self.pad_entries += 1
            return self.pad_entries
        return None

    def inject_idle_reads(self, label, *, pad=1):
        entry = None
        for _ in range(EDGE_IDLE_READS):
            entry = self.inject(IDLE, label, pad=pad)
        return entry

    def samples(self):
        path = self.out / "pad-samples.live.txt"
        return path.read_text(errors="replace") if path.exists() else ""

    def wait_sample(self, port, entry):
        sample = re.compile(sample_pattern(port, entry), re.M)
        while not sample.search(self.samples()):
            self.service(self.text())
            time.sleep(0.05)
        self.check_sampled_exit()

    def press_until_effect(self, value, label, pattern, *, pad=1):
        mark = len(self.text())
        effect = SampledEffect(label, pad, self.inject(value, label, pad=pad))
        regex = re.compile(pattern, re.M)
        while True:
            text = self.text()
            self.check_sampled_exit()
            match = regex.search(text, mark)
            if effect.observed(self.samples(), match):
                return match.end()
            self.service(text)
            time.sleep(0.1)

    def wait_sampled_films(self, names):
        films = SampledFilms(names)
        while films.name is not None:
            text = self.text()
            self.service(text)
            films(self, text)
            time.sleep(0.2)

    # -- waiting
    def service(self, text):
        if self.pad2 is None:
            self.pruner.run(text)
        for servicer in self.servicers:
            servicer(self, text)
        if "tr64 fatal:" in text:
            raise GateFailure("cart fatal: " + re.search(r"tr64 fatal:[^\r\n]*", text)[0])
        if self.proc.poll() is not None:
            raise GateFailure(f"emulator exited early (status {self.proc.returncode})")
        now = time.monotonic()
        if self.deadline is None:
            if (self.out / "slot.txt").exists():
                self.deadline, self.last_progress = now + self.timeout + 120, now
            elif now > self.queued_until:
                raise GateFailure("no ares slot became free")
            return
        size = len(text)
        if size != self.log_size:
            self.log_size, self.last_progress = size, now
        if now > self.deadline:
            raise GateFailure("deadline")
        if now - self.last_progress > SILENCE_LIMIT:
            raise GateFailure(f"emulator log silent for {SILENCE_LIMIT} s")

    def check_sampled_exit(self):
        if self.pad2 is not None and self.proc.poll() not in (None, 0):
            raise GateFailure(f"pad harness failed (status {self.proc.returncode})")

    def wait(self, pattern, *, after=0):
        """Offset just past the first match of `pattern` at or after `after`."""
        regex = re.compile(pattern, re.M)
        while True:
            text = self.text()
            match = regex.search(text, after)
            if match:
                self.check_sampled_exit()
                return match.end()
            try:
                self.service(text)
            except GateFailure as failure:
                raise GateFailure(f"{failure} while waiting for /{pattern}/") from None
            time.sleep(0.2)

    def wait_ticks(self, count):
        target = self.tick() + count
        while self.tick() < target:
            self.service(self.text())
            time.sleep(0.1)
        self.check_sampled_exit()

    def wait_frames(self, after, count):
        """Until `count` game ticks have been logged after offset `after` (ticks do not advance while a
        level loads, so a count taken before a level change would include ticks of the old level)."""
        regex = re.compile(r"^play-frame: ", re.M)
        while len(regex.findall(self.text(), after)) < count:
            self.service(self.text())
            time.sleep(0.1)
        self.check_sampled_exit()

    def press(self, value, label, hold=3, gap=4):
        self.inject(value, label)
        self.wait_ticks(hold)
        self.inject(IDLE, "release")
        self.wait_ticks(gap)

    # -- the end
    def stop(self, *, check=True):
        """TERM the whole group (the emulator exports SRAM on TERM) and consume the final log."""
        terminating = self.proc.poll() is None
        if terminating:
            try:
                if self.pad2 is None:
                    os.killpg(self.proc.pid, signal.SIGTERM)
                else:
                    # The launcher stops the remote emulator and reaps the relay.
                    # Signaling the entire group would kill the local relay ssh first.
                    self.proc.terminate()
            except ProcessLookupError:
                terminating = False
        try:
            self.proc.wait(timeout=stop_grace(60))
        except subprocess.TimeoutExpired:
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait(timeout=stop_grace(15))
        if self.pad2 is None:
            self.pruner.run(self.text())
        elif check and self.proc.returncode != (143 if terminating else 0):
            raise GateFailure(f"pad harness failed (status {self.proc.returncode})")


# A VI sample is not a game poll. Assume at most 200 ms between guest
# polls (one 66.7 ms period + recorded 92.3 ms decoder step + 33.3 ms
# service interval; not a measured upper bound). Thirty-two 60 Hz replies
# span 517 ms, >2.5x that assumption. If the game still misses neutral
# and the press has no effect, SampledEffect fails the gate closed.
EDGE_IDLE_READS = 32


def sample_pattern(port, entry):
    return (rf"^TESTHARNESS pad-sampled port={port} entry={entry} "
            r"buttons=[0-9a-f]{8} stick=-?\d+,-?\d+ ns=\d+\n")


def sampled(text, port, entry):
    return re.search(sample_pattern(port, entry), text, re.M) is not None


class SampledFilms:
    """Prime neutral against asynchronous guest reads, then hold START until the skip."""

    def __init__(self, names):
        self.pending = iter(names)
        self.name, self.state, self.entry = next(self.pending, None), "waiting", None
        self.effect = None

    def __call__(self, drive, text):
        name, state, entry = self.name, self.state, self.entry
        if name is None:
            return
        ended = re.search(rf"^video: name={re.escape(name)} result=(\w+) ", text, re.M)
        if ended and state in ("waiting", "idle"):
            raise GateFailure(f"sampled START failed to skip {name} (result={ended[1]}, stage={state}, entry={entry})")
        if state == "waiting" and f"video: begin name={name} " in text:
            self.entry = drive.inject_idle_reads(f"prime idle after begin {name}")
            self.state = "idle"
        elif state == "idle" and sampled(drive.samples(), 1, entry):
            label = f"Start: skip {name}"
            self.effect = SampledEffect(label, 1, drive.inject(word("START"), label))
            self.state = "pressed"
        elif state == "pressed":
            failed = f"film result was {ended[1]}" if ended and ended[1] != "skipped" else None
            if self.effect.observed(drive.samples(), ended, failed):
                self.entry = drive.inject_idle_reads(f"release {name}")
                self.state = "release"
        elif state == "release" and sampled(drive.samples(), 1, entry):
            self.name, self.state = next(self.pending, None), "waiting"


def skip_films(names):
    """Servicer for runs that are not the video verdict: START skips each named film at its
    start (about 15 s of film otherwise cost minutes on a loaded host), released at its end."""
    state = {}

    def service(drive, text):
        for name in names:
            began = f"video: begin name={name} " in text
            ended = f"video: name={name} result=" in text
            if began and name not in state:
                drive.inject(word("START"), f"Start: skip {name}")
                state[name] = "pressed"
            elif ended and state.get(name) == "pressed":
                drive.inject(IDLE, f"release {name}")
                state[name] = "released"
    return service
