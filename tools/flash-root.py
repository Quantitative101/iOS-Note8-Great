#!/usr/bin/env python3
"""Write the raw iOS root image to the Note 8's USERDATA through TWRP's adb.

  flash-root.py root.img [--state FILE] [--dry-run]

The image is cut into 64 MiB chunks. A chunk of all zeros is written on the phone from /dev/zero (this also
clears old USERDATA data in the image's range); any other chunk is gzip'd, pushed to /tmp, unpacked into the
partition at its offset with dd, and read back for a SHA-256 comparison. --state records finished chunks so
an interrupted run resumes. Refuses to run unless TWRP is up and /data is unmounted.
Only the first len(image) bytes of USERDATA are touched.
"""
import argparse
import gzip
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time

DEV = '/dev/block/platform/11120000.ufs/by-name/USERDATA'
CHUNK = 64 << 20
MIB = 1 << 20


def adb(args, check=True):
    for attempt in range(6):
        p = subprocess.run(['adb', *args], capture_output=True, text=True)
        transient = 'daemon' in p.stderr or 'device offline' in p.stderr or \
            'no devices' in p.stderr or 'cannot connect' in p.stderr
        if not (p.returncode and transient):
            break
        print(f'adb transient error, retry {attempt + 1}: {p.stderr.strip()[:120]}', flush=True)
        time.sleep(5)
        subprocess.run(['adb', 'wait-for-recovery'], capture_output=True, timeout=120)
    if check and p.returncode:
        raise SystemExit(f'adb {" ".join(args)} failed: {p.stdout} {p.stderr}')
    return p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('--state')
    ap.add_argument('--dry-run', action='store_true', help='read and classify chunks, write nothing')
    args = ap.parse_args()

    size = os.path.getsize(args.image)
    assert size % MIB == 0, 'image size must be a multiple of 1 MiB'
    nchunks = (size + CHUNK - 1) // CHUNK
    done = set()
    if args.state and os.path.exists(args.state):
        done = set(json.load(open(args.state)))
    if not args.dry_run:
        mounts = adb(['shell', 'mount'])
        if ' /data ' in mounts and DEV.rsplit('/', 1)[0] not in '':
            if any(l.split()[2] == '/data' for l in mounts.splitlines() if len(l.split()) > 2):
                raise SystemExit('/data is mounted: unmount it in TWRP (Mount menu) first')
        part = adb(['shell', f'blockdev --getsize64 {DEV}']).strip()
        print(f'USERDATA: {int(part) / MIB:.0f} MiB, image {size / MIB:.0f} MiB, {nchunks} chunks', flush=True)
        if int(part) < size:
            raise SystemExit('image larger than the partition')
    zero = bytes(CHUNK)
    t0 = time.time()
    sent = skipped = 0
    with open(args.image, 'rb') as f:
        for i in range(nchunks):
            data = f.read(CHUNK)
            off_mib = i * (CHUNK // MIB)
            if i in done:
                continue
            n_mib = (len(data) + MIB - 1) // MIB
            is_zero = data == zero[:len(data)]
            if args.dry_run:
                skipped += is_zero
                sent += not is_zero
                continue
            if is_zero:
                adb(['shell', f'dd if=/dev/zero of={DEV} bs=1048576 seek={off_mib} count={n_mib} 2>/dev/null'])
                check = hashlib.sha256(zero[:len(data)]).hexdigest()
            else:
                with tempfile.NamedTemporaryFile(suffix='.gz') as tmp:
                    with gzip.GzipFile(fileobj=tmp, mode='wb', compresslevel=1) as g:
                        g.write(data)
                    tmp.flush()
                    adb(['push', tmp.name, '/tmp/chunk.gz'])
                adb(['shell', f'gunzip -c /tmp/chunk.gz | dd of={DEV} bs=1048576 seek={off_mib} 2>/dev/null; sync'])
                check = hashlib.sha256(data).hexdigest()
            back = adb(['shell', f'dd if={DEV} bs=1048576 skip={off_mib} count={n_mib} 2>/dev/null | sha256sum']).split()[0]
            if back != check:
                raise SystemExit(f'chunk {i} (offset {off_mib} MiB) read back wrong: {back} != {check}')
            sent += not is_zero
            skipped += is_zero
            done.add(i)
            if args.state:
                json.dump(sorted(done), open(args.state, 'w'))
            el = time.time() - t0
            print(f'chunk {i + 1}/{nchunks} ok ({"zero" if is_zero else "data"}), {el:.0f}s', flush=True)
    print(f'{"dry run: " if args.dry_run else ""}{sent} data chunks, {skipped} zero chunks, {nchunks} total')


if __name__ == '__main__':
    main()
