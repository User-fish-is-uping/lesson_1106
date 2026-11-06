#!/usr/bin/env python3
"""Validate frame JSONL and replay through the same C core; no Python counting logic."""
import argparse
import ctypes as C
import json
import math
import sys
from pathlib import Path

U64 = (1 << 64) - 1

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
    count, events, ins, outs = 0, 0, 0, 0
    with args.input.open(encoding='utf-8-sig') as src:
        for line_no, line in enumerate(src, 1):
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
                if not isinstance(obj, dict) or not isinstance(obj.get('kind'), str):
                    raise ValueError('expected object with kind')
                if obj['kind'] != 'frame':
                    continue
                data = frame(obj)
            except (ValueError, KeyError, TypeError, OverflowError) as err:
                print(f'{args.input}:{line_no}: {err}', file=sys.stderr)
                return 2
            out = (C.c_uint64 * 6)()
            has = lib.replay_frame(*data, out)
            count += 1
            ins, outs = out[3], out[4]
            if out[5]:
                print(json.dumps({'kind': 'diagnostic', 'line': line_no, 'error': out[5]}))
            if has:
                events += 1
                print(json.dumps({'kind': 'event', 'id': out[0], 't_ms': out[1],
                                  'type': 'ENTER' if out[2] == 1 else 'EXIT', 'in': ins, 'out': outs}))
    print(json.dumps({'kind': 'summary', 'frames': count, 'events': events, 'in': ins, 'out': outs}))
    return 0 if count else 3

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError, TypeError, OverflowError) as err:
        print(err, file=sys.stderr)
        sys.exit(2)
