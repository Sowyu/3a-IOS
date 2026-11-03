#!/usr/bin/env python3
"""Run one frozen, reviewed data image and restore the original network profile."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
PREF = '/private/var/preferences/SystemConfiguration/preferences.plist'
HOLD = Path('/tmp/phone-modem-wants')
LOCK = Path('/tmp/phone.lock')
DEV = ROOT / 'plans/devloop/dev.py'
PUT = ROOT / 'plans/devloop/06-hfs-put.py'
PULL = ROOT / 'ios-asteroids/tools/sweet-hfs-pull.py'
os.environ['TMPDIR'] = '/tmp'
sys.dont_write_bytecode = True


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local(name):
    path = (ROOT / name).resolve()
    if not path.is_relative_to(ROOT):
        raise ValueError('artifact outside workspace')
    return path


def validate(m):
    if m.get('approved') is not True:
        raise ValueError('exact-image Sol approval is not recorded')
    if not re.fullmatch(r'[a-f0-9]{32}', m['nonce']):
        raise ValueError('invalid nonce')
    for name, want in m['hashes'].items():
        if digest(local(name)) != want:
            raise ValueError('frozen artifact changed: ' + name)
    for key in ('image', 'script', 'profile', 'original', 'audit', 'args', 'root_script'):
        if m[key] not in m['hashes']:
            raise ValueError('unpinned artifact: ' + key)
    if not local(m['image']).name.startswith('data-usb-'):
        raise ValueError('not a data image')
    if m.get('test') != 'modem-data':
        raise ValueError('wrong selftest')
    if not re.fullmatch(r'modem-data-[0-9]+', m['note']):
        raise ValueError('invalid run note')


def run(argv, timeout=300):
    return subprocess.run(argv, capture_output=True, timeout=timeout, check=True)


def busy():
    r = subprocess.run(['pgrep', '-f', '[d]evloop/dev.py'], capture_output=True, text=True)
    if r.returncode not in (0, 1):
        raise RuntimeError('process discovery failed')
    return [int(p) for p in r.stdout.split()]


def recovery():
    lines = run(['adb', 'devices'], 10).stdout.decode().splitlines()[1:]
    devices = [line.split() for line in lines if line.strip()]
    return devices[0][0] if len(devices) == 1 and devices[0][1:] == ['recovery'] else None


def clean(serial):
    return run([sys.executable, '-I', str(PUT), '--adb', serial, '--state']).stdout.strip() == b'clean'


def pull_profile(serial, out, hook=False):
    out.mkdir()
    result = run([sys.executable, '-I', str(PULL), '-s', serial, str(out), PREF] + (['/usr/local/libexec/renegade-firstboot.sh'] if hook else []))
    (out / 'read.log').write_bytes(result.stdout + result.stderr)
    return out / 'preferences.plist'


def restore(serial, out, original):
    if busy() or recovery() != serial or not clean(serial):
        raise RuntimeError('restoration requires the same phone in clean recovery')
    before = pull_profile(serial, out / 'before-restore')
    if before.read_bytes() != original.read_bytes():
        result = run([sys.executable, '-I', str(PUT), '--adb', serial,
                      '--undo-dir', str(out), PREF + '=' + str(original)])
        (out / 'restore.log').write_bytes(result.stdout + result.stderr)
    after = pull_profile(serial, out / 'after-restore')
    if after.read_bytes() != original.read_bytes() or not clean(serial):
        raise RuntimeError('restored root/profile verification failed')


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: run-data-usb.py REVIEWED-MANIFEST.json')
    manifest = local(sys.argv[1])
    m = json.loads(manifest.read_text())
    validate(m)
    # No lock or phone mutation before all host identity checks pass.
    spec = importlib.util.spec_from_file_location('modem_devloop', DEV)
    dev = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(dev)
    args = dev.bootargs_of(local(m['image']))
    if args != local(m['args']).read_text().strip():
        raise RuntimeError('image arguments differ from the reviewed line')
    if dev.check_loop_args(args) != (None, 900):
        raise RuntimeError('wrong image deadman or dev guards')
    expected = run([sys.executable, str(DEV), '--print-script', 'modem-data']).stdout
    if expected != local(m['script']).read_bytes():
        raise RuntimeError('current hook differs from approved script')
    attempt = manifest.with_suffix('.attempt')
    if attempt.exists():
        raise RuntimeError('one-boot scope already consumed')
    HOLD.touch()
    print('Waiting for an idle phone in recovery', flush=True)
    while True:
        for pid in busy():
            while True:
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(2)
        serial = recovery()
        if serial and not busy():
            try:
                LOCK.mkdir()
                break
            except FileExistsError:
                pass
        time.sleep(5)
    safe_release = False
    launched = False
    out = ROOT / 'plans/modem/out' / ('data-run-' + m['nonce'])
    try:
        if busy() or recovery() != serial or not clean(serial):
            raise RuntimeError('phone state changed before launch')
        out.mkdir()
        actual = pull_profile(serial, out / 'before', hook=True)
        if actual.read_bytes() != local(m['original']).read_bytes():
            raise RuntimeError('live profile differs from the approved original')
        body = (ROOT / 'plans/devloop/selftest/scripts/modem-data.sh').read_text()
        base = (out / 'before/renegade-firstboot.sh').read_text()
        if dev.compose_script(body, base).encode() != local(m['root_script']).read_bytes():
            raise RuntimeError('actual root hook would differ from the approved bytes')
        validate(m)
        with attempt.open('x') as f:
            f.write('one attempt consumed\n')
        launched = True
        command = [sys.executable, str(DEV), str(local(m['image'])),
                   PREF + '=' + str(local(m['profile'])), '--no-replay',
                   '--boot-tries', '1', '--button-wait', '0', '--test', 'modem-data',
                   '--note', m['note'], '--key', 'SWEET MODEM|SWEET IPA|SWEET DATA',
                   '--pull', '/private/var/tmp/modem-data-complete.txt']
        # dev.py bounds each operation and uses the independent 900-second deadman.
        with (out / 'devloop.log').open('wb') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        return result.returncode
    finally:
        if launched:
            try:
                restore(serial, out, local(m['original']))
            except BaseException:
                print('Ownership retained pending recovery and profile restoration', flush=True)
                raise
            safe_release = True
        else:
            safe_release = True
        if safe_release:
            run(['/usr/bin/trash', str(LOCK)], 30)
            run(['/usr/bin/trash', str(HOLD)], 30)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('Stopped: ' + str(error), file=sys.stderr)
        sys.exit(2)
