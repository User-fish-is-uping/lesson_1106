"""Verify actual adapter JSON roundtrips through the unmodified cloud C replay."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
executable, library, scenario = sys.argv[1:]
p = subprocess.run([executable, scenario], text=True, capture_output=True, check=True)
rows = [json.loads(line) for line in p.stdout.splitlines()]
config = next(row['greeter'] for row in rows if row['kind'] == 'config')
declared = json.loads((root/'firmware/vision_count/config.json').read_text())
assert set(config) == set(declared)
for key, value in declared.items():
    if isinstance(value, float):
        assert struct.pack('f', config[key]) == struct.pack('f', value), key
    else:
        assert config[key] == value, key
frames = [row for row in rows if row['kind'] == 'frame']
for frame in frames:
    assert len(frame['persons']) <= 10
    for person in frame['persons']:
        assert person['score'] > declared['confidence_threshold'], 'person score below configured threshold'
        assert struct.pack('f', person['score']) == struct.pack('I', 0x3F000001)
        for coordinate in ('x1', 'x2', 'y1', 'y2'):
            assert 0 <= person[coordinate] <= 1
events = [row for row in rows if row['kind'] == 'event']
if scenario == 'roundtrip':
    assert [(e['type'], e['id'], e['t_ms']) for e in events] == [('ENTER', 1, 750), ('EXIT', 2, 1350)]
else:
    assert not events
audio = [row for row in rows if row['kind'] == 'audio']
assert len(audio) == len(events)
for event, item in zip(events, audio):
    assert item['status'] == 'muted' and item['id'] == event['id']
    assert item['t_ms'] == event['t_ms'] + 20, 'queue must use current service time'
out = root/'build/adapter-fixtures'
out.mkdir(parents=True, exist_ok=True)
log = out/f'synthetic_{scenario}.jsonl'
log.write_text(p.stdout, encoding='utf-8')
cfg = out/'config-from-adapter.json'
cfg.write_text(json.dumps(config, indent=2), encoding='utf-8')
replay = subprocess.run([sys.executable, str(root/'tools/host/replay.py'), str(log),
                         '--library', library, '--config', str(cfg)],
                        capture_output=True, text=True, check=True)
replayed = [json.loads(line) for line in replay.stdout.splitlines()]
replayed_events = [row for row in replayed if row['kind'] == 'event']
shared = ('kind', 'id', 't_ms', 'type', 'in', 'out')
assert [{key: row[key] for key in shared} for row in replayed_events] == events
assert all(row['last_event_id'] == row['id'] for row in replayed_events)
assert [row['queued'] for row in replayed_events] == list(range(1, len(events) + 1))
last = [row for row in rows if row['kind'] == 'state'][-1]
assert replayed[-1]['in'] == last['in'] and replayed[-1]['out'] == last['out']
with tempfile.TemporaryDirectory(dir=out) as temporary:
    tmp = Path(temporary).resolve()
    assert tmp.is_relative_to(out.resolve()), 'Temporary cleanup must stay in build output'
    raw = tmp/'raw.txt'
    raw.write_text('course driver startup text\n' + p.stdout, encoding='utf-8')
    extract = [sys.executable, str(root/'tools/windows/extract-counter-log.py'), str(raw)]
    subprocess.run(extract + [str(tmp/'extracted')], capture_output=True, text=True, check=True)
    assert (tmp/'extracted/capture.jsonl').read_text() == p.stdout
    assert json.loads((tmp/'extracted/config.json').read_text()) == config
    raw.write_text('{"kind":"boot"}\n{"kind":"boot"}\n' + p.stdout, encoding='utf-8')
    assert subprocess.run(extract + [str(tmp/'multi')], capture_output=True).returncode == 2
    raw.write_text(p.stdout + '{broken\n', encoding='utf-8')
    assert subprocess.run(extract + [str(tmp/'broken')], capture_output=True).returncode == 2
print(f'{scenario}: adapter events, float precision, config and C replay agree (synthetic)')
