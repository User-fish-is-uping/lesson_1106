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
assert rows[-1] == dict(kind='summary', frames=9, controls=0, events=2,
                       **{'in':1, 'out':1}, last_event_id=2, reverse=False,
                       queued=2, muted=False, audio_dropped_muted=0,
                       audio_cleared=0, audio_dropped_full=0, audio_expired=0)
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

with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'config.json'
    cfg=json.loads((root/'tests/data/default_config.json').read_text())
    path.write_text(json.dumps(cfg))
    args=base+['--config',str(path),str(root/'tests/data/synthetic_roundtrip.jsonl')]
    p=subprocess.run(args,capture_output=True,text=True)
    assert p.returncode==0,p.stderr
    assert json.loads(p.stdout.splitlines()[-1])['events']==2
    cfg['outer_stable_ms']=300
    path.write_text(json.dumps(cfg))
    p=subprocess.run(args,capture_output=True,text=True)
    assert p.returncode==0 and json.loads(p.stdout.splitlines()[-1])['events']==0,(p.stdout,p.stderr)
    cfg['line_b']=0.1
    path.write_text(json.dumps(cfg))
    p=subprocess.run(args,capture_output=True,text=True)
    assert p.returncode==2 and 'rejected' in p.stderr
print('Explicit config, changed stability time, and invalid config tests passed')

p = subprocess.run(base+[str(root/'tests/data/synthetic_controls.jsonl')],
                   capture_output=True, text=True)
assert p.returncode == 0, p.stderr
rows = [json.loads(s) for s in p.stdout.splitlines()]
events = [r for r in rows if r['kind'] == 'event']
assert [(r['id'], r['type']) for r in events] == [
    (1, 'ENTER'), (2, 'EXIT'), (3, 'ENTER'), (4, 'EXIT')], rows
controls = [r for r in rows if r['kind'] == 'control']
assert [r['action'] for r in controls] == ['reset', 'reverse', 'mute', 'mute', 'reset']
assert controls[-1]['last_event_id'] == 3 and controls[-1]['in'] == controls[-1]['out'] == 0
summary = rows[-1]
assert summary['frames'] == 23 and summary['controls'] == 5 and summary['events'] == 4
assert summary['last_event_id'] == 4 and summary['in'] == 0 and summary['out'] == 1
assert summary['reverse'] is True and summary['muted'] is False
assert summary['audio_dropped_muted'] == 1 and summary['audio_cleared'] == 2
assert summary['queued'] == 1
print('Control order, reset/reverse, mute recovery, queue clearing, and event continuity passed')

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)/'bad-control.jsonl'
    bad = [
        '{"kind":"control","t_ms":1,"action":"unknown"}\n',
        '{"kind":"control","t_ms":1,"action":"mute","value":1}\n',
        '{"kind":"control","action":"reset"}\n',
        '{"kind":"mystery","t_ms":1}\n',
    ]
    for content in bad:
        path.write_text(content)
        p = subprocess.run(base+[str(path)], capture_output=True, text=True)
        assert p.returncode == 2 and ':1:' in p.stderr, (content, p.stdout, p.stderr)
print('Invalid controls and unknown record kinds include line-numbered errors')

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)/'chronology.jsonl'
    sample = {"kind":"frame", "seq":1, "t_ms":10, "valid":True,
              "truncated":False, "persons":[]}
    path.write_text('\n'.join([json.dumps(sample), json.dumps(sample)])+'\n')
    p = subprocess.run(base+[str(path)], capture_output=True, text=True)
    assert p.returncode == 0, p.stderr
    rows = [json.loads(s) for s in p.stdout.splitlines()]
    diagnostic = next(r for r in rows if r['kind'] == 'diagnostic')
    assert diagnostic == {'kind':'diagnostic', 'line':2,
                          'error':'OLD_FRAME', 'error_code':3}
print('Core chronology diagnostics retain source line and symbolic error')
