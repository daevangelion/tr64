# SPDX-License-Identifier: MIT
"""Exercise the real remote launcher (local ssh/Xvfb/ares stand-ins)."""
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / 'test/run/ares.sh'


def script(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#!/bin/bash\nset -eu\n' + text)
    path.chmod(0o755)


def controls(root):
    home, bin_dir = root / 'home', root / 'bin'
    home.mkdir()
    local = dict(os.environ, PATH=f'{bin_dir}:{os.environ["PATH"]}', FIXTURE_HOME=str(home),
                 TR64_ARES_HOST='fixture', TR64_ARES_REMOTE_SLOTS='1', TMPDIR=str(root))
    script(bin_dir / 'ssh', '''
while [[ ${1:-} == -o ]]; do shift 2; done
[[ ${1:-} != -n ]] || shift
shift
export HOME=$FIXTURE_HOME
cd "$HOME"
if [[ -n ${FIXTURE_TAMPER:-} && "$*" == *' --remote-side '* ]]; then
 read -r _ _ _ id _ rom _ <<<"$*"
 printf corrupt >"tr64-runs/$id/rom/$rom"
fi
if [[ "$*" == *' --remote-side '* ]]; then
 exec python3 -c 'import os,signal,sys
os.setsid()
for s in (signal.SIGTERM, signal.SIGINT): signal.signal(s, signal.SIG_DFL)
os.execvp("bash", ["bash", "-c", sys.argv[1]])' "$*"
fi
exec bash -c "$*"
''')
    script(bin_dir / 'Xvfb', 'printf "0\\n" >&3\n')
    identity = subprocess.check_output(['bash', '-c',
        '. "$1/identity.sh"; sampled_ares_identity "$1"', '_', ROOT / 'tools/ares'], text=True).strip()
    rom = root / 'rom.z64'
    rom.write_bytes(b'ROM bytes\n')
    actual = home / 'tr64-ares-coop'
    binary = actual / 'build/desktop-ui/ares'
    (actual / 'xdg').mkdir(parents=True)

    def install(marker, *, announce=None):
        script(binary, f''': <<'IDENTITY'
TR64 sampled ares {marker}
IDENTITY
printf 'fixture emulator started\\n'
''' + (f"printf 'TR64 sampled ares {announce}\\n' >>\"$TR64_ARES_SAMPLE_LOG\"\n" if announce else "") + r'''
if [[ "$FIXTURE_FINISH" == wait ]]; then exec sleep 40; fi
if [[ "$FIXTURE_FINISH" == missing-hello ]]; then exit 0; fi
if [[ "$FIXTURE_FINISH" == wrong-hello ]]; then exec sleep 40; fi
exec python3 - <<'PY'
import os
from pathlib import Path
import sys
import time
mode = os.environ['FIXTURE_FINISH']
for i, key in enumerate(('ARES_TEST_INPUT', 'ARES_TEST_INPUT_2'), 1):
    pad = Path(os.environ[key])
    deadline = time.monotonic() + 5
    while len(fields := pad.read_text().split()) != 4:
        if time.monotonic() >= deadline: sys.exit(41)
        time.sleep(.005)
    mask, x, y, seq = fields
    stamp = time.monotonic_ns()
    if i == 2 and mode == 'late': stamp += 31_000_000_000
    end = '' if i == 2 and mode == 'unterminated' else '\n'
    # The production producer's stream is separate from partially written game rows.
    with open(os.environ['TR64_ARES_SAMPLE_LOG'], 'a') as log:
        print(f'TESTHARNESS pad-sampled port={i} entry={seq} buttons={mask} stick={x},{y} ns={stamp}', end=end, file=log)
sys.exit(19 if mode == 'ordinary-error' else 0)
PY
''')
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        (actual / 'sampled-identity.txt').write_text(f'{identity} {digest}\n')

    def run(name, expected, *options, sig=None, tamper=False, admitted=False, finish='wait', legacy=False,
            journal='pads-v2\n1 0 0 0\n2 0 0 0\n'):
        out = root / name
        out.mkdir()
        for stale in ("slot.txt", "ares-host.txt", "ares-identity.txt"):
            (out / stale).write_text("stale\n")
        pad1, pad2 = (out / p for p in (options or ('pad', 'pad2')))
        for pad in (pad1, pad2):
            pad.write_text('0 0 0\n')
        pad1.with_name(pad1.name + '.seq').write_text(journal)
        log = out / 'ares.log'
        env = dict(local, ARES_TEST_INPUT=str(pad1), ARES_TEST_INPUT_2=str(pad2), FIXTURE_TAMPER='1' if tamper else '')
        env['FIXTURE_FINISH'] = finish
        if legacy:
            env.pop('ARES_TEST_INPUT_2')
            pad1.with_name(pad1.name + '.seq').write_text('0 0 0\n')
        with subprocess.Popen([LAUNCHER, out, rom, log, '40'], env=env, start_new_session=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as p:
            try:
                if sig:
                    deadline = time.monotonic() + 10
                    while not log.is_file() or 'fixture emulator started' not in log.read_text():
                        assert p.poll() is None and time.monotonic() < deadline, name
                        time.sleep(.02)
                    # A real relay has an in-flight entry, and the fake emulator never samples it.
                    while not any(x.read_text().endswith(' 1\n') for x in home.glob('tr64-runs/*/input/pad')
                                  if not (x.parents[1] / 'exit').exists()):
                        assert p.poll() is None and time.monotonic() < deadline, name
                        time.sleep(.02)
                    p.send_signal(sig)
                _, err = p.communicate(timeout=16)
            except BaseException:
                p.terminate()
                p.communicate(timeout=16)
                raise
        assert p.returncode == expected, (name, p.returncode, err)
        for receipt in ('slot.txt', 'ares-host.txt', 'ares-identity.txt'):
            assert (out / receipt).exists() == (bool(sig) or admitted), (name, receipt, err)
        if finish in ('missing-hello', 'wrong-hello'):
            candidates = sorted(home.glob('tr64-runs/*/input/pad'), key=lambda p: p.stat().st_mtime_ns)
            last_pad = candidates[-1]
            assert last_pad.read_text() == '0 0 0\n' and last_pad.with_name('pad2').read_text() == '0 0 0\n'
        return err

    assert b'ROM SHA-256 mismatch' in run('tamper', 65, tamper=True)
    assert b'ROM SHA-256 mismatch' in run('legacy-tamper', 65, tamper=True, legacy=True)
    assert b'sampled ares missing or mismatched' in run('missing', 67)
    install('0' * 64)
    assert b'sampled ares missing or mismatched' in run('wrong-embedded', 67)
    install(identity, announce=identity)
    prev = (actual / 'sampled-identity.txt').read_text()
    (actual / 'sampled-identity.txt').write_text(f'{identity} {"0" * 64}\n')
    assert b'sampled ares missing or mismatched' in run('wrong-exe-hash', 67)
    (actual / 'sampled-identity.txt').write_text(prev)
    assert b'pad-unsampled port=2' in run('missing-pad2', 66, journal='pads-v2\n1 0 0 0\n')
    assert b'staging names must be distinct' in run('alias', 2, 'pad', 'pad.tmp')
    assert b'pad-unsampled port=2 entry=2' in run('late', 66, admitted=True, finish='late')
    assert b'pad-unsampled port=2 entry=2' in run('unterminated', 66, admitted=True, finish='unterminated')
    run('normal', 0, admitted=True, finish='ok')
    run('emulator-status', 19, admitted=True, finish='ordinary-error')
    install(identity)
    assert b'runtime identity missing' in run('missing-hello', 67, admitted=True, finish='missing-hello')
    install(identity, announce='0' * 64)
    assert b'runtime identity missing or mismatched' in run('wrong-hello', 67, admitted=True, finish='wrong-hello')
    install(identity, announce=identity)
    run('term', 143, sig=signal.SIGTERM)
    run('int', 130, sig=signal.SIGINT)
    print('sampled launcher PASS: admission and runtime identity, terminal relay verdict and complete/timely ack, '
          'ordinary/stopped launcher status precedence')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='sampled-launcher-') as directory:
        controls(Path(directory))
