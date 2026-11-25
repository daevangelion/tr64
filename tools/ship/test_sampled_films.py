# SPDX-License-Identifier: MIT
"""SI replies and game polls are independently timed."""
from unittest.mock import patch
from drive import (EDGE_IDLE_READS, SAMPLED_EFFECT_LIMIT, Drive, GateFailure, IDLE, SampledEffectFailure,
                   SampledFilms, skip_films, word)


class Guest:
    def __init__(self, idle_reads=EDGE_IDLE_READS, poll_every=9):
        self.journal = []
        self.text = ""
        self.replies = ""
        self.held = None
        self.name = None
        self.skipped = []
        self.reads = self.entries = 0
        self.idle_reads = idle_reads
        self.poll_every = poll_every
        self.live = word("START")  # held when the game enters a fresh state

    def begin(self, name):
        self.name, self.held = name, None
        self.text += f"video: begin name={name} stream_blocks=1\n"

    def samples(self):
        return self.replies

    def inject(self, value, label, *, pad=1):
        assert pad == 1
        self.entries += 1
        self.journal.append((self.entries, value))
        return self.entries

    def inject_idle_reads(self, label):
        last = None
        for _ in range(self.idle_reads):
            last = self.inject(IDLE, label)
        return last

    def sample(self):
        self.reads += 1
        if self.journal:
            entry, self.live = self.journal.pop(0)
            buttons = int(self.live.split()[0], 16)
            self.replies += f"TESTHARNESS pad-sampled port=1 entry={entry} buttons={buttons:08x} stick=0,0 ns={self.reads}\n"
        # The first game poll is delayed; hardware samples can already have
        # acknowledged (and overwritten) the neutral state by then.
        if not self.name or self.reads % self.poll_every:
            return
        if self.held == IDLE and self.live == word("START"):
            self.text += f"video: name={self.name} result=skipped pictures=0\n"
            self.skipped.append(self.name)
            self.name = None
        self.held = self.live


if __name__ == "__main__":
    films = ("ESCAPE", "CORE")
    guest, service = Guest(), SampledFilms(films)
    for name in films:
        guest.begin(name)
        service(guest, guest.text)
        assert len(guest.journal) == EDGE_IDLE_READS and guest.journal[0][1] == IDLE
        for _ in range(120):
            guest.sample()
            service(guest, guest.text)
        assert not guest.journal
    assert guest.skipped == list(films)
    for idle_reads in (0, 1, 8):
        old = Guest(idle_reads)
        bare = SampledFilms(films) if idle_reads else skip_films(films)
        # Phase the first game poll just after the short neutral sequence.
        old.reads = 0 if idle_reads < 8 else 9
        old.begin(films[0])
        for _ in range(40):
            bare(old, old.text)
            old.sample()
        assert old.held == word("START") and not old.skipped, (idle_reads, old.skipped)
    print("sampled films PASS: two films with asynchronous game polls, no frame rows; bare and short idle fail")

    slow = Guest(poll_every=EDGE_IDLE_READS + 2)
    slow.begin(films[0])
    service = SampledFilms(films)
    with patch("drive.time.monotonic", return_value=0) as clock:
        service(slow, slow.text)
        for _ in range(2 * slow.poll_every):
            slow.sample()
            service(slow, slow.text)
        assert slow.held == word("START") and not slow.skipped and service.state == "pressed"
        clock.return_value = SAMPLED_EFFECT_LIMIT
        try:
            service(slow, slow.text)
        except SampledEffectFailure as error:
            assert error.exit_status == 68 and f"port=1 entry={EDGE_IDLE_READS + 1} action=Start: skip ESCAPE" in str(error)
        else:
            raise AssertionError("guest above neutral-poll assumption did not fail closed")

    no_effect = Drive.__new__(Drive)
    no_effect.text = lambda: "play: no menu transition\n"
    no_effect.samples = lambda: "TESTHARNESS pad-sampled port=2 entry=39 buttons=10000000 stick=0,0 ns=11\n"
    no_effect.inject = lambda value, label, pad: 39
    no_effect.service = lambda text: None
    no_effect.check_sampled_exit = lambda: None
    with patch("drive.time.monotonic", side_effect=(0, SAMPLED_EFFECT_LIMIT)), patch("drive.time.sleep"):
        try:
            no_effect.press_until_effect(word("START"), "open ring", r"^menu opened$", pad=2)
        except SampledEffectFailure as error:
            assert error.exit_status == 68 and "port=2 entry=39 action=open ring:" in str(error)
        else:
            raise AssertionError("sampled ring press with no effect did not fail closed")
    print("sampled effect PASS: a missed film edge or ring action fails by its own deadline (68)")

    class Stopped:
        returncode = 67

        def poll(self):
            return self.returncode

        def wait(self, *, timeout):
            return self.returncode

    stopped = Drive.__new__(Drive)
    stopped.pad2, stopped.proc = True, Stopped()
    stopped.text = lambda: "play-frame: tick=40\nlast match\n"
    for verdict in (lambda: stopped.wait(r"last match"), lambda: stopped.wait_ticks(0),
                    lambda: stopped.wait_frames(0, 1), stopped.stop):
        try:
            verdict()
        except GateFailure as error:
            assert "status 67" in str(error)
        else:
            raise AssertionError("final log match hid a nonzero launcher status")
    print("sampled driver PASS: existing final records cannot hide launcher failure")
