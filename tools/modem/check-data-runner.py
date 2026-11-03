#!/usr/bin/env python3
"""Host-only checks for frozen identity and fail-closed profile restoration."""
import importlib.util
import tempfile
import sys
sys.dont_write_bytecode = True
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('runner', Path(__file__).with_name('run-data-usb.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
base = Path(tempfile.mkdtemp(prefix='runner-', dir=r.ROOT / 'plans/modem/out'))
original = base / 'original'
original.write_bytes(b'original')
after = base / 'after'
after.write_bytes(b'original')
changed = base / 'changed'
changed.write_bytes(b'changed')
class Result:
    stdout = b'ok'
    stderr = b''

def must_fail(fn):
    try:
        fn()
    except (ValueError, RuntimeError):
        return
    raise AssertionError('unsafe operation was accepted')

for processes, device, clean in (([123], 'fixture', True), ([], None, True), ([], 'other', True), ([], 'fixture', False)):
    with patch.object(r, 'busy', return_value=processes), patch.object(r, 'recovery', return_value=device), patch.object(r, 'clean', return_value=clean), patch.object(r, 'run') as run:
        must_fail(lambda: r.restore('fixture', base, original))
        run.assert_not_called()
with patch.object(r, 'busy', return_value=[]), patch.object(r, 'recovery', return_value='fixture'), patch.object(r, 'clean', return_value=True):
    with patch.object(r, 'pull_profile', side_effect=[after, after]), patch.object(r, 'run') as run:
        r.restore('fixture', base, original)
        run.assert_not_called()
    with patch.object(r, 'pull_profile', side_effect=[changed, after]), patch.object(r, 'run', return_value=Result()) as run:
        r.restore('fixture', base, original)
        command = run.call_args.args[0]
        assert '--undo-dir' in command and command[-1] == r.PREF + '=' + str(original)
        assert '--undo' not in command and '--dev' not in command
    with patch.object(r, 'pull_profile', side_effect=[changed, changed]), patch.object(r, 'run', return_value=Result()):
        must_fail(lambda: r.restore('fixture', base, original))

files = {key: base / ('data-usb-2208.img' if key == 'image' else key) for key in ('image', 'script', 'profile', 'original', 'audit', 'args', 'root_script')}
for p in files.values():
    if not p.exists():
        p.write_bytes(b'fixture')
m = {key: str(p.relative_to(r.ROOT)) for key, p in files.items()}
m.update(approved=True, nonce='a' * 32, test='modem-data', note='modem-data-2208')
m['hashes'] = {str(p.relative_to(r.ROOT)): r.digest(p) for p in files.values()}
r.validate(m)
for key, value in (('approved', False), ('nonce', 'bad'), ('test', 'other'), ('note', 'unsafe name'), ('image', '/tmp/outside')):
    bad = dict(m, **{key: value})
    must_fail(lambda: r.validate(bad))
files['script'].write_bytes(b'changed')
must_fail(lambda: r.validate(m))
print('PASS frozen identity, approval gate, clean same-phone restoration and readback failure')
