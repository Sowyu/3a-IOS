#!/usr/bin/env python3
"""Build cleanup: move only build-tree objects to recoverable Trash."""
import os
import subprocess
import sys
from pathlib import Path
ROOT = Path('/Users/aniko/Documents/Vibes/EverIOS/ios-asteroids/research/xnu-modem/dxb')
force = False
operands = []
options = True
for arg in sys.argv[1:]:
    if options and arg == '--':
        options = False
    elif options and arg.startswith('-'):
        if not arg[1:] or any(c not in 'frR' for c in arg[1:]):
            raise SystemExit('cleanup: unsupported option')
        force |= 'f' in arg
    else:
        operands.append(arg)
if not operands:
    raise SystemExit(0 if force else 'cleanup: no operands')
for arg in operands:
    p = Path(arg.rstrip('/'))
    if p.name in ('', '.', '..'):
        raise SystemExit('cleanup: unsafe operand')
    p = p.parent.resolve() / p.name
    if not p.is_relative_to(ROOT) or p == ROOT or p == ROOT / 'xnu':
        raise SystemExit('cleanup: outside allowed build paths: ' + str(p))
    if not os.path.lexists(p):
        if force:
            continue
        raise SystemExit('cleanup: missing path: ' + str(p))
    if Path(sys.argv[0]).name == 'rmdir' and (p.is_symlink() or not p.is_dir() or any(p.iterdir())):
        raise SystemExit('cleanup: rmdir requires empty directory')
    subprocess.run(['/usr/bin/trash', str(p)], check=True)
