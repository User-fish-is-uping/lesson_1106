"""Exercise the real WAV loader and DMA lifecycle with host hardware stubs."""
import pathlib
import struct
import subprocess
import sys
import tempfile

def chunk(name, data):
    return name + struct.pack('<I', len(data)) + data + (b'\0' if len(data) % 2 else b'')

def wav(rate=16000, channels=1, pcm=b'\x00\x00\xff\x7f\x00\x80' * 100, extra=b''):
    fmt = struct.pack('<HHIIHH', 1, channels, rate, rate * channels * 2, channels * 2, 16)
    body = b'WAVE' + extra + chunk(b'fmt ', fmt) + chunk(b'data', pcm)
    return b'RIFF' + struct.pack('<I', len(body)) + body

cases = [
    ('valid', wav(), 'valid'),
    ('odd_metadata', wav(extra=chunk(b'JUNK', b'abc')), 'valid'),
    ('wrong_rate', wav(rate=22050), 'invalid'),
    ('stereo', wav(channels=2), 'invalid'),
    ('too_long', wav(pcm=b'\0' * 96002), 'invalid'),
    ('empty', wav(pcm=b''), 'invalid'),
    ('odd_pcm', wav(pcm=b'abc'), 'invalid'),
    ('truncated', wav()[:-1], 'invalid'),
    ('short_header', b'RIFF', 'invalid'),
    ('oversized_chunk', b'RIFF\x0c\0\0\0WAVEdata\xff\xff\xff\xff', 'invalid'),
    ('allocation_failure', wav(), 'nomem'),
]
with tempfile.TemporaryDirectory() as folder:
    for name, data, expected in cases:
        path = pathlib.Path(folder) / (name + '.wav')
        path.write_bytes(data)
        subprocess.run([sys.argv[1], str(path), expected], check=True)
    subprocess.run([sys.argv[1], str(pathlib.Path(folder) / 'missing.wav'), 'invalid'], check=True)
print('12 audio loader/lifecycle cases passed (simulated hardware).')
