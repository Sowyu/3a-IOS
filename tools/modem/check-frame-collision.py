#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[3]
tmp=Path(tempfile.mkdtemp(prefix='archive-collision-',dir=root/'plans/modem/out'))
try:
 hook=(root/'plans/devloop/selftest/hook.sh').read_text()
 block=hook[hook.index('\t# Retain earlier'):hook.index('\tsay "start"')]
 (tmp/'date').write_text('#!/bin/sh\necho 1781738941\n');(tmp/'date').chmod(0o755)
 script='''#!/bin/sh
set -eu
ST_OUT="'''+str(tmp)+'''/frames"
say(){ printf '%s\\n' "$*"; }
archive(){
'''+block+'''
}
mkdir "$ST_OUT"
printf first > "$ST_OUT/frame"
archive
printf second > "$ST_OUT/frame"
archive
printf third > "$ST_OUT/frame"
archive
'''
 (tmp/'test.sh').write_text(script)
 subprocess.run(['sh',str(tmp/'test.sh')],check=True,env={'PATH':str(tmp)+':/usr/bin:/bin'})
 saved=list(tmp.glob('frames.saved.*'));assert len(saved)==3
 assert sorted((p/'frame').read_text() for p in saved)==['first','second','third']
 assert not list((tmp/'frames').iterdir())
 print('PASS repeated time/PID archives preserve all three generations')
finally:subprocess.run(['/usr/bin/trash',str(tmp)],check=True)
