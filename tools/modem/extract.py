"""Reconstruct the tested modem source from its ordered public patches."""
from pathlib import Path
import re
import sys
source = []
for path in sorted(Path(sys.argv[1]).glob('217*.patch')):
    out, cursor, active, in_hunk = [], 0, False, False
    for line in path.read_text().splitlines(True):
        if line.startswith('--- '):
            in_hunk = False
        elif line.startswith('+++ '):
            active = line.rstrip() == '+++ b/bsd/dev/sweet_modem.c'
        elif active and line.startswith('@@ '):
            match = re.match(r'@@ -(\d+)(?:,\d+)? ', line)
            assert match, line
            start = max(0, int(match[1]) - 1)
            assert start >= cursor
            out.extend(source[cursor:start]); cursor = start; in_hunk = True
        elif active and in_hunk:
            if line.startswith((' ', '-')):
                assert cursor < len(source) and source[cursor] == line[1:], (path, cursor)
                cursor += 1
            if line.startswith((' ', '+')):
                out.append(line[1:])
    if out:
        source = out + source[cursor:]
assert source
Path(sys.argv[2]).write_text(''.join(source))
