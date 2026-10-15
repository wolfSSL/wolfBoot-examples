#!/usr/bin/env python3
"""Print any button/switch event from the carrier. No root needed (group input).
Run on the board, press every button you can find, Ctrl-C when done."""
import struct, sys, select
FMT = 'llHHi'; SZ = struct.calcsize(FMT)
TYPES = {1: 'KEY', 5: 'SWITCH'}
devs = {}
for n in (0, 1):
    try:
        devs[open(f'/dev/input/event{n}', 'rb', buffering=0)] = f'event{n}'
    except OSError as e:
        print(f'event{n}: {e}')
if not devs:
    sys.exit('no readable input devices')
print('watching:', ', '.join(devs.values()), '- press buttons now')
try:
    while True:
        r, _, _ = select.select(list(devs), [], [], 60)
        if not r:
            print('(60s with no events)')
            continue
        for f in r:
            sec, usec, typ, code, val = struct.unpack(FMT, f.read(SZ))
            if typ in TYPES:
                print(f'  {devs[f]}  {TYPES[typ]} code={code} value={val}'
                      f'  {"PRESS" if val else "release"}')
except KeyboardInterrupt:
    print('\ndone')
