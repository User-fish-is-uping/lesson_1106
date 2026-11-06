import json
import subprocess
import sys
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[1]
base = [sys.executable, str(root/'tools/host/replay.py'), '--library', sys.argv[1]]
p = subprocess.run(base+[str(root/'tests/data/synthetic_roundtrip.jsonl')], capture_output=True, text=True)
assert p.returncode == 0, p.stderr
rows = [json.loads(s) for s in p.stdout.splitlines()]
assert [r['type'] for r in rows if r['kind']=='event'] == ['ENTER','EXIT'], rows
assert rows[-1] == dict(kind='summary',frames=9,events=2,**{'in':1,'out':1})
p = subprocess.run(base+['--reverse',str(root/'tests/data/synthetic_roundtrip.jsonl')], capture_output=True,text=True)
assert p.returncode == 0, p.stderr
assert [json.loads(s)['type'] for s in p.stdout.splitlines() if json.loads(s)['kind']=='event'] == ['EXIT','ENTER']
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)/'bad.jsonl'
    for content,code in [('{}\n',2),('{"kind":"frame","seq":-1}\n',2),('not json\n',2),('{"kind":"metadata"}\n',3)]:
        path.write_text(content)
        p = subprocess.run(base+[str(path)],capture_output=True,text=True)
        assert p.returncode==code,(content,p.stdout,p.stderr)
        if code==2: assert ':1:' in p.stderr
print('JSONL roundtrip, reverse, invalid input, and zero-frame checks passed')
