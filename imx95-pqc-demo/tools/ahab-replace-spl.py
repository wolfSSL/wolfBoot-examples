#!/usr/bin/env python3
"""Put a wolfBoot stage 1 into AHAB container 0, in place of U-Boot SPL.

The container's other images - the ELE firmware, the System Manager and the
OEI that trains DDR - are copied through byte for byte, and so is container 1.
Only the SPL slot's payload, size and hash change, and because the slot is
followed by a zero-length end marker at a fixed offset the container's overall
size does not move, so container 1 stays where it was.

  ahab-replace-spl.py <base image> <loader_stage1.bin> <output image>

The base image is a full eMMC boot-partition image (or any image holding an
AHAB container set at 0x8000). The image array records a SHA-384 over each
image, so the replaced slot's hash is recomputed here; the boot ROM rejects
the container otherwise.
"""
import hashlib
import struct
import sys

CTNR0 = 0x8000
SPL_DST = 0x20480000
IMG_ENTRY = 128
HDR = 16
ALIGN = 0x400


def main(argv):
    if len(argv) != 4:
        sys.stderr.write(__doc__)
        return 1
    base, stage1_path, out = argv[1], argv[2], argv[3]

    buf = bytearray(open(base, 'rb').read())
    stage1 = open(stage1_path, 'rb').read()

    if buf[CTNR0 + 3] != 0x87:
        sys.stderr.write("no container at 0x%x\n" % CTNR0)
        return 1
    count = buf[CTNR0 + 11]

    slot = None
    for i in range(count):
        e = CTNR0 + HDR + i * IMG_ENTRY
        dst, = struct.unpack_from('<Q', buf, e + 8)
        size, = struct.unpack_from('<I', buf, e + 4)
        if dst == SPL_DST and size != 0:
            slot = i
            break
    if slot is None:
        sys.stderr.write("no image loading to 0x%x\n" % SPL_DST)
        return 1

    e = CTNR0 + HDR + slot * IMG_ENTRY
    off, old_size = struct.unpack_from('<II', buf, e)
    new_size = (len(stage1) + ALIGN - 1) & ~(ALIGN - 1)
    if new_size > old_size:
        sys.stderr.write("stage 1 is %d bytes, slot holds %d\n"
                         % (new_size, old_size))
        return 1

    start = CTNR0 + off
    buf[start:start + old_size] = b'\0' * old_size
    buf[start:start + len(stage1)] = stage1

    struct.pack_into('<I', buf, e + 4, new_size)
    # The image array records a SHA-384 of exactly the recorded size.
    digest = hashlib.sha384(bytes(buf[start:start + new_size])).digest()
    buf[e + 32:e + 96] = digest + b'\0' * (64 - len(digest))

    open(out, 'wb').write(bytes(buf))
    print("slot %d: 0x%x bytes at container offset 0x%x (was 0x%x)"
          % (slot, new_size, off, old_size))
    print("sha384 : %s" % digest.hex())
    print("output : %s (%d bytes)" % (out, len(buf)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
