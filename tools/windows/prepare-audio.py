"""Convert recorded PCM WAV to the firmware's mono/16 kHz/16-bit format.
Requires Python 3.12 (audioop); never overwrites an existing output.
"""
import argparse
import audioop
import pathlib
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('input', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
if args.output.exists():
    parser.error('Output already exists; choose a new path.')
with wave.open(str(args.input), 'rb') as src:
    if src.getcomptype() != 'NONE' or src.getnchannels() not in (1, 2):
        parser.error('Use uncompressed mono or stereo PCM WAV.')
    if not src.getnframes() or src.getnframes() > src.getframerate() * 3:
        parser.error('Recording must be nonempty and no longer than 3 seconds; trim it first.')
    data = src.readframes(src.getnframes())
    width = src.getsampwidth()
    if width == 1:
        data = audioop.bias(data, 1, -128)
    if src.getnchannels() == 2:
        data = audioop.tomono(data, width, .5, .5)
    data = audioop.lin2lin(data, width, 2)
    data, _ = audioop.ratecv(data, 2, 1, src.getframerate(), 16000, None)
if not data or len(data) > 96000:
    parser.error('Converted recording must be nonempty and no longer than 3 seconds.')
with args.output.open('xb') as target:
    with wave.open(target, 'wb') as out:
        out.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        out.writeframes(data)
print(f'{args.output}: mono PCM16, 16000 Hz, {len(data)/32000:.3f} s')
