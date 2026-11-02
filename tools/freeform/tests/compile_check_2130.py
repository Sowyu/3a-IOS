#!/usr/bin/env python3
"""Applies 2130 to a temp copy of kern_exec.c and compiles it with the real build's flags (kern_exec.o.json), -fsyntax-only.
Writes nothing to the kernel tree. Run: python3 -I plans/freeform/tests/compile_check_2130.py"""
import collections, json, os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, '../../..'))
K = os.path.join(ROOT, 'ios-asteroids/research/xnu-local/dxb/xnu')
OBJ = os.path.join(ROOT, 'ios-asteroids/research/xnu-local/dxb/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT')
P = os.path.join(HERE, '../2130-asteroids-app-metal-exec.patch')
env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
r = subprocess.run(['git', 'apply', '--check', P], cwd=K, capture_output=True, text=True)
print(('ok   ' if r.returncode == 0 else 'FAIL ') + 'git apply --check in the kernel tree ' + r.stderr.strip()[:200])
bad = r.returncode != 0
with tempfile.TemporaryDirectory() as t:
    d = os.path.join(t, 'k/bsd/kern'); os.makedirs(d)
    shutil.copy(os.path.join(K, 'bsd/kern/kern_exec.c'), d)
    subprocess.run(['git', 'init', '-q', '.'], cwd=os.path.join(t, 'k'), check=True)
    subprocess.run(['git', 'apply', P], cwd=os.path.join(t, 'k'), check=True)
    db = json.JSONDecoder().raw_decode(open(os.path.join(OBJ, 'kern_exec.o.json')).read().lstrip())[0]
    db = db[0] if isinstance(db, list) else db
    cmd, skip = [], False
    for a in db['arguments']:
        if skip: skip = False; continue
        if a == '-o' or a in ('-MF', '-MT', '-MQ'): skip = True; continue
        if a == '-c' or a.endswith('/kern_exec.c') or a in ('-MD', '-MMD', '-MP') or a.startswith(('-gen-cdb', '--serialize', '-MJ')): continue
        cmd.append(a)
    # quoted includes of the original directory still resolve
    def warnings(src):
        r = subprocess.run(cmd + ['-I' + os.path.join(K, 'bsd/kern'), '-fsyntax-only', src], cwd=db.get('directory', OBJ),
                           capture_output=True, text=True, env=env)
        # startup macros put the line number in an identifier's name: a moved line is not a new warning
        msgs = [re.sub(r"_\d+'", "_N'", l.split(': ', 1)[1]) for l in r.stderr.splitlines()
                if 'kern_exec.c:' in l and (' warning:' in l or ' error:' in l)]
        return r.returncode, msgs
    rc, new = warnings(os.path.join(d, 'kern_exec.c'))
    _, old = warnings(os.path.join(K, 'bsd/kern/kern_exec.c'))
    print(('ok   ' if rc == 0 else 'FAIL ') + 'kern_exec.c with 2130 compiles with the build flags (-fsyntax-only)')
    added = list((collections.Counter(new) - collections.Counter(old)).elements())   # counted: a second copy of an old warning is new
    print(('ok   ' if not added else 'NOTE ') + '%d diagnostics the patch adds' % len(added))
    for m in added: print('     ' + m[:220])
    bad |= rc != 0
sys.exit(bad)
