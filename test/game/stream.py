# SPDX-License-Identifier: MIT
"""Bounded-log subprocess with a dedicated trace pipe and deadline."""
import atexit
import os
import signal
import subprocess
import sys
import threading

RUNNING = set()


def stop_children():
    for run in list(RUNNING):
        run.kill()


atexit.register(stop_children)
signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))


class Run:
    def __init__(self, command, output_option, log_limit=8192, pass_fds=()):
        read_fd, write_fd = os.pipe()
        self.process = subprocess.Popen(command + [output_option, f'/proc/self/fd/{write_fd}'],
            pass_fds=(*pass_fds, write_fd), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        os.close(write_fd)
        self.trace = os.fdopen(read_fd, 'rb')
        self.log = bytearray()
        self.log_limit = log_limit
        self.reader = threading.Thread(target=self.drain, daemon=True)
        self.reader.start()
        RUNNING.add(self)
        self.timer = threading.Timer(60, self.kill)
        self.timer.daemon = True
        self.timer.start()

    def kill(self):
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def drain(self):
        for chunk in iter(lambda: self.process.stdout.read(4096), b''):
            self.log.extend(chunk)
            if len(self.log) > self.log_limit:
                del self.log[:-self.log_limit]

    def finish(self, kill=False):
        if kill:
            self.kill()
        code = self.process.wait(timeout=5)
        self.timer.cancel()
        RUNNING.discard(self)
        self.reader.join(timeout=2)
        self.trace.close()
        return code, self.log.decode(errors='replace')
