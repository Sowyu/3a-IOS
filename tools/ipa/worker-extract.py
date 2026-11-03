from pathlib import Path
import subprocess as sp
H=Path(__file__).resolve().parent;O=H/'tests/out';s=(O/'ModemRuntime.inc').read_text()
# Apply only runtime hunks onto the reconstructed modem owner source.
import re
for p in sorted((H.parents[1]/'patches/ipa').glob('22*.patch')):
 source=s.splitlines(True);out=[];cursor=0;active=False;hunk=False
 for line in p.read_text().splitlines(True):
  if line.startswith(('diff --git','--- ')):hunk=False
  elif line.startswith('+++ '):active=line.rstrip()=='+++ b/bsd/dev/modem/ModemRuntime.inc'
  elif active and line.startswith('@@ '):
   start=max(0,int(re.match(r'@@ -(\d+)',line)[1])-1);assert start>=cursor;out+=source[cursor:start];cursor=start;hunk=True
  elif active and hunk:
   if line.startswith((' ','-')):assert source[cursor]==line[1:],(p,cursor);cursor+=1
   if line.startswith((' ','+')):out.append(line[1:])
 if out:s=''.join(out+source[cursor:])
(O/'ModemRuntime.inc').write_text(s)
sections=[('static void ipa_transport_sync(', 'static void m_uim_readiness('),('static void m_uim_refresh(', 'static bool m_uim_endpoint_valid('),('static void m_greceive(', 'static int m_glink_prepare(')]
# Extract exact functions with brace-balanced boundaries instead of adjacent helpers.
def function(name):
 match=re.search(r'^static [^\n;]*\b'+re.escape(name)+r'[^;{]*\{',s,re.M);assert match,name
 start=match.start();a=match.end()-1;depth=1;b=a+1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[start:b]+'\n'
(O/'worker-functions.inc').write_text(''.join(function(n) for n in ['ipa_transport_sync(', 'ipa_data_quiesce(', 'm_uim_refresh(', 'm_greceive(', 'm_gstate(']))
