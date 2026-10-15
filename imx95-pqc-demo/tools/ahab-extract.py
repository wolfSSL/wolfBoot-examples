#!/usr/bin/env python3
"""List and extract the images in an i.MX95 AHAB container set.

The boot medium holds a set of containers: the first one the boot ROM reads at
0x8000, and further ones immediately after. Nothing records where the next
container starts - it is the end of the previous one (the furthest of its
header, its images and its signature block) rounded up to 1 KiB.

Use this to recover the SoC's own images from a stock boot image, which is
where BL31 and OP-TEE come from when rebuilding the container set:

  ahab-extract.py boot0.bin                 # list what is there
  ahab-extract.py boot0.bin -o out/         # write each image out

Images are named by their load address, which is what identifies them:
BL31, OP-TEE and BL33 all sit at fixed addresses on this SoC.
"""
import argparse
import os
import struct
import sys

MMC_OFFSET = 0x8000
ALIGN = 1024
TAG = 0x87
VERSION = 0x02
HDR = 16
ENTRY = 128

# Load addresses are stable on i.MX95, so they name the image.
KNOWN = {
    0x8A200000: ("bl31.bin", "ARM Trusted Firmware BL31 (secure monitor, EL3)"),
    0x8C000000: ("tee.bin", "OP-TEE (trusted OS, secure EL1)"),
    0x90200000: ("bl33.bin", "BL33 (U-Boot proper, or wolfBoot)"),
    0x20480000: ("spl.bin", "A55 image in OCRAM (U-Boot SPL, or wolfBoot stage 1)"),
    0x1FFC0000: ("m33.bin", "Cortex-M33 image (System Manager or OEI)"),
}


def parse_container(buf, base):
    """Return (images, size) for the container at base, or None."""
    if base + HDR > len(buf):
        return None
    version, length, tag = buf[base], struct.unpack_from('<H', buf, base + 1)[0], buf[base + 3]
    if tag != TAG or version != VERSION:
        return None
    count = buf[base + 11]
    if count == 0 or count > 8:
        return None
    sig_off = struct.unpack_from('<H', buf, base + 12)[0]

    end = length
    images = []
    for i in range(count):
        e = base + HDR + i * ENTRY
        off, size = struct.unpack_from('<II', buf, e)
        dst, entry = struct.unpack_from('<QQ', buf, e + 8)
        flags = struct.unpack_from('<I', buf, e + 24)[0]
        if size > 0xFFFFFFFF - off:
            return None
        end = max(end, off + size)
        images.append(dict(index=i, offset=off, size=size, dst=dst,
                           entry=entry, flags=flags))
    if sig_off:
        sig_len = struct.unpack_from('<H', buf, base + sig_off + 1)[0]
        end = max(end, sig_off + sig_len)
    return images, end


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', help='boot image holding an AHAB container set')
    ap.add_argument('-o', '--out-dir', help='write each non-empty image here')
    ap.add_argument('--base', type=lambda s: int(s, 0), default=MMC_OFFSET,
                    help='offset of the first container (default 0x8000)')
    args = ap.parse_args()

    buf = open(args.image, 'rb').read()
    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)

    base, n, found = args.base, 0, 0
    used = set()
    while True:
        got = parse_container(buf, base)
        if got is None:
            if n == 0:
                sys.stderr.write("no AHAB container at 0x%x\n" % base)
                return 1
            break
        images, size = got
        print("container %d at 0x%x, %d image(s), %d bytes" % (n, base, len(images), size))
        for im in images:
            name, desc = KNOWN.get(im['dst'], (None, "unknown"))
            if im['size'] == 0:
                print("  [%d] empty marker at +0x%x" % (im['index'], im['offset']))
                continue
            print("  [%d] 0x%08X  %8d bytes  %s" % (im['index'], im['dst'], im['size'], desc))
            if args.out_dir:
                fname = name or ("image_0x%08X.bin" % im['dst'])
                # Two images can share a load address - the OEI runs and
                # returns before the System Manager is loaded over it - so
                # disambiguate rather than silently overwriting.
                if fname in used:
                    stem, ext = os.path.splitext(fname)
                    fname = "%s_c%di%d%s" % (stem, n, im['index'], ext)
                used.add(fname)
                out = os.path.join(args.out_dir, fname)
                start = base + im['offset']
                with open(out, 'wb') as f:
                    f.write(buf[start:start + im['size']])
                print("       -> %s" % out)
                found += 1
        base = (base + size + ALIGN - 1) & ~(ALIGN - 1)
        n += 1

    if args.out_dir:
        print("\nextracted %d image(s) to %s" % (found, args.out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
