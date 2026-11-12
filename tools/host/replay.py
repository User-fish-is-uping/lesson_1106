#!/usr/bin/env python3
"""Validate frame JSONL and replay through the same C core; no Python counting logic."""
import argparse
import ctypes as C
import json
import math
import sys
from pathlib import Path

U64 = (1 << 64) - 1
STATE_SIZE = 14
PASSIVE_KINDS = {
    'config', 'event', 'state', 'audio', 'metadata', 'fixture',
    'boot', 'camera', 'model', 'audio_init', 'audio_player'
}
ERROR_NAMES = {
    0: 'OK', 1: 'BAD_CONFIG', 2: 'BAD_INPUT', 3: 'OLD_FRAME',
    4: 'TIME_BACKWARD', 5: 'TRACK_JUMP', 6: 'TIMEOUT', 7: 'OVERFLOW'
}

def integer(v, name):
    if type(v) is not int or not 0 <= v <= U64:
        raise ValueError(f'{name}: expected uint64')
    return v

def frame(obj):
    seq, time = integer(obj['seq'], 'seq'), integer(obj['t_ms'], 't_ms')
    if type(obj['valid']) is not bool or type(obj['truncated']) is not bool:
        raise ValueError('valid/truncated must be boolean')
    persons = obj['persons']
    if not isinstance(persons, list) or len(persons) > 10:
        raise ValueError('persons must be list with at most 10 items')
    values = []
    for p in persons:
        row = [p[k] for k in ('x1', 'y1', 'x2', 'y2', 'score')]
        if any(type(v) not in (float, int) or not math.isfinite(v) or not 0 <= v <= 1 for v in row):
            raise ValueError('box coordinates/score must be finite within [0,1]')
        if row[0] >= row[2] or row[1] >= row[3]:
            raise ValueError('degenerate box')
        values.extend(row)
    return seq, time, obj['valid'], obj['truncated'], len(persons), (C.c_float * 50)(*values)

def control(obj):
    action = obj.get('action')
    time = integer(obj['t_ms'], 't_ms')
    if action == 'reset':
        if set(obj) != {'kind', 't_ms', 'action'}:
            raise ValueError('reset control requires exactly kind/t_ms/action')
        return 0, 0, time
    if action not in ('reverse', 'mute'):
        raise ValueError(f'unknown control action: {action!r}')
    if set(obj) != {'kind', 't_ms', 'action', 'value'} or type(obj.get('value')) is not bool:
        raise ValueError(f'{action} control requires boolean value')
    return (1 if action == 'reverse' else 2), int(obj['value']), time

def state_dict(values):
    return {
        'in': values[3], 'out': values[4], 'last_event_id': values[6],
        'reverse': bool(values[7]), 'queued': values[8], 'muted': bool(values[9])
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--library', type=Path, default=Path('build/host/libgreeter_replay.so'))
    parser.add_argument('--reverse', action='store_true', help='toggle configured direction')
    parser.add_argument('--config', type=Path, help='complete greeter config JSON')
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    lib.replay_init.argtypes = [C.c_int]
    lib.replay_init.restype = C.c_int
    lib.replay_frame.argtypes = [C.c_uint64, C.c_uint64, C.c_int, C.c_int, C.c_uint,
                                C.POINTER(C.c_float), C.POINTER(C.c_uint64)]
    lib.replay_frame.restype = C.c_int
    lib.replay_control.argtypes = [C.c_uint, C.c_int, C.c_uint64, C.POINTER(C.c_uint64)]
    lib.replay_control.restype = C.c_int
    lib.replay_get_state.argtypes = [C.POINTER(C.c_uint64)]
    lib.replay_get_state.restype = None
    if args.config:
        cfg = json.loads(args.config.read_text(encoding='utf-8-sig'))
        floats = ('line_a', 'line_b', 'hysteresis', 'confidence_threshold',
                  'association_tolerance', 'max_speed_per_s')
        times = ('outer_stable_ms', 'lost_grace_ms', 'traversal_timeout_ms')
        if not isinstance(cfg, dict) or set(cfg) != set(floats + times + ('axis', 'reverse_direction')):
            raise ValueError('config must contain exactly the documented greeter fields')
        if cfg['axis'] not in ('X', 'Y') or type(cfg['reverse_direction']) is not bool:
            raise ValueError('config axis must be X/Y and reverse_direction boolean')
        values = [cfg[k] for k in floats]
        if any(type(v) not in (int, float) or not math.isfinite(v) for v in values):
            raise ValueError('config float fields must be finite numbers')
        milliseconds = [integer(cfg[k], k) for k in times]
        lib.replay_configure.argtypes = [C.c_uint, C.POINTER(C.c_float), C.POINTER(C.c_uint64), C.c_int]
        lib.replay_configure.restype = C.c_int
        result = lib.replay_configure(cfg['axis'] == 'Y', (C.c_float * 6)(*values),
                                      (C.c_uint64 * 3)(*milliseconds), cfg['reverse_direction'] ^ args.reverse)
    else:
        result = lib.replay_init(args.reverse)
    if result:
        raise ValueError('configuration rejected by C core')
    count, controls, events = 0, 0, 0
    values = (C.c_uint64 * STATE_SIZE)()
    with args.input.open(encoding='utf-8-sig') as src:
        for line_no, line in enumerate(src, 1):
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
                if not isinstance(obj, dict) or not isinstance(obj.get('kind'), str):
                    raise ValueError('expected object with kind')
                if obj['kind'] == 'control':
                    action, value, time = control(obj)
                    if lib.replay_control(action, value, time, values):
                        raise ValueError('control rejected by C bridge')
                    controls += 1
                    row = {'kind': 'control', 'line': line_no, 't_ms': time,
                           'action': obj['action'], **state_dict(values)}
                    if obj['action'] != 'reset':
                        row['value'] = obj['value']
                    print(json.dumps(row))
                    continue
                if obj['kind'] in PASSIVE_KINDS:
                    continue
                if obj['kind'] != 'frame':
                    raise ValueError(f'unknown kind: {obj["kind"]!r}')
                data = frame(obj)
            except (ValueError, KeyError, TypeError, OverflowError) as err:
                print(f'{args.input}:{line_no}: {err}', file=sys.stderr)
                return 2
            values = (C.c_uint64 * STATE_SIZE)()
            has = lib.replay_frame(*data, values)
            count += 1
            if values[5]:
                print(json.dumps({'kind': 'diagnostic', 'line': line_no,
                                  'error': ERROR_NAMES.get(values[5], f'UNKNOWN_{values[5]}'),
                                  'error_code': values[5]}))
            if has:
                events += 1
                print(json.dumps({'kind': 'event', 'id': values[0], 't_ms': values[1],
                                  'type': 'ENTER' if values[2] == 1 else 'EXIT',
                                  **state_dict(values)}))
    lib.replay_get_state(values)
    summary = {'kind': 'summary', 'frames': count, 'controls': controls, 'events': events,
               **state_dict(values), 'audio_dropped_muted': values[10],
               'audio_cleared': values[11], 'audio_dropped_full': values[12],
               'audio_expired': values[13]}
    print(json.dumps(summary))
    return 0 if count or controls else 3

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError, TypeError, OverflowError) as err:
        print(err, file=sys.stderr)
        sys.exit(2)
