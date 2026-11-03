"""Execute the production push method: dirty admission must never boot or write."""
import ast
from pathlib import Path
from types import SimpleNamespace
import sys
p=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[3]/'tools/devloop/dev.py'
tree=ast.parse(p.read_text())
fn=next(n for cls in tree.body if isinstance(cls,ast.ClassDef) for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='push')
class Stop(Exception):pass
scope={'Stop':Stop}
exec(compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),str(p),'exec'),scope)
for state in ['dirty','state read failed','journal unknown']:
 obj=SimpleNamespace(a=SimpleNamespace(no_replay=True),root_state=lambda serial:state)
 result={}
 try:scope['push'](obj,'mock',None,result)
 except Stop as e:assert '--no-replay' in str(e)
 else:raise AssertionError('dirty root admitted')
 assert result['root_before']==state
assert "ap.add_argument('--no-replay'" in p.read_text()
print('PASS: dirty/unreadable root stops before any boot or write')
