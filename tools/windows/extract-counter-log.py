"""Keep the original serial capture; extract one boot's JSONL and actual config."""
import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='original saved serial text (UTF-8)')
    parser.add_argument('output', type=Path, help='new output directory')
    args = parser.parse_args()
    lines, configs, boots, frames, skipped = [], [], 0, 0, 0
    for number, line in enumerate(args.input.read_text(encoding='utf-8-sig', errors='replace').splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith('{'):
            skipped += 1
            continue
        try:
            row = json.loads(line)
        except ValueError as error:
            raise ValueError(f'line {number}: broken JSON; keep raw input and inspect: {error}') from error
        if not isinstance(row, dict) or not isinstance(row.get('kind'), str):
            raise ValueError(f'line {number}: JSON object missing kind')
        lines.append(line)  # Preserve the original decimal digits, do not reformat floats.
        boots += row['kind'] == 'boot'
        frames += row['kind'] == 'frame'
        if row['kind'] == 'config':
            configs.append(row['greeter'])
    if boots > 1 or len(configs) != 1:
        raise ValueError('Expected exactly one config and at most one boot; split captures by reset')
    if not frames:
        raise ValueError('No frame records found')
    if args.output.exists():
        raise ValueError('Output directory already exists; use a new directory to preserve evidence')
    args.output.mkdir(parents=True)
    (args.output/'capture.jsonl').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (args.output/'config.json').write_text(json.dumps(configs[0], indent=2) + '\n', encoding='utf-8')
    print(f'Extracted {frames} frames; skipped {skipped} non-JSON driver lines. Original capture unchanged.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
