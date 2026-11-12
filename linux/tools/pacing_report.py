#!/usr/bin/env python3
"""The times in a pacing trace of mh4u-linux (MH4N_PACING_TRACE=<file>), in milliseconds.

    python3 linux/tools/pacing_report.py TRACE

The trace is a sequence of 16-byte records, little endian: u64 CLOCK_MONOTONIC nanoseconds, u32 kind, u32 value
(mh4n/native/include/mh4n/pacing_trace.h). It reports, with the median, 90th and 99th percentile and the largest:

  host read -> game sample   a change of the buttons the host read (kind 100, linux_main.cpp's kHostButtons) to the
                             game's sample of the same buttons (kInputSample): how long a press waits for the game
  publish -> picture taken   the renderer's publication of a game frame (kPublish) to the host taking its top-screen
                             picture (kAppPicture, the same game frame)
  picture taken -> presented the host taking a top-screen picture to its SDL_RenderPresent returning (kAppPresentOut)
  present call               the time inside SDL_RenderPresent of the top window (kAppPresentIn to kAppPresentOut)
  game sample -> presented   a button sample (kInputSample) to the first top-screen present that returned after the
                             next publication: the earliest the press can be on the top screen's picture (the game
                             may show it some frames later; the panel's scan-out comes after this)
"""
import struct
import sys

PUBLISH, APP_PICTURE, APP_PRESENT_IN, APP_PRESENT_OUT = 6, 7, 8, 9  # (pacing_trace.h)
HOST_BUTTONS = 100  # (linux_main.cpp)
KINDS_FILE = "mh4n/native/include/mh4n/pacing_trace.h"


def input_sample_kind(root):
    """kInputSample's number, read from pacing_trace.h (it is not among the first kinds)."""
    import os
    import re
    path = os.path.join(root, KINDS_FILE)
    with open(path, encoding="utf-8") as f:
        m = re.search(r"kInputSample\s*=\s*(\d+)", f.read())
    if not m:
        raise SystemExit(f"no kInputSample in {path}")
    return int(m.group(1))


def stats(name, values):
    if not values:
        print(f"{name:28s} no samples")
        return
    v = sorted(values)
    pick = lambda q: v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))]
    print(f"{name:28s} n={len(v):6d}  median {pick(0.5):7.2f}  p90 {pick(0.9):7.2f}  p99 {pick(0.99):7.2f}  "
          f"max {v[-1]:7.2f}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    import os
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    sample_kind = input_sample_kind(root)
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    entries = [struct.unpack_from("<QII", data, i) for i in range(0, len(data) - len(data) % 16, 16)]
    entries.sort(key=lambda e: e[0])
    ms = lambda a, b: (b - a) / 1e6

    host_to_sample, publish_to_picture, picture_to_present, present_call, sample_to_present = [], [], [], [], []
    pending_host = {}        # buttons -> time the host read them, waiting for the game's sample
    publish_at = {}          # game frame -> time of its publication
    picture_at = None        # time of the last picture taken, waiting for its present
    present_in_at = None
    pending_samples = []     # sample times waiting for a publication
    waiting_present = []     # sample times whose frame was published, waiting for a present
    for ns, kind, value in entries:
        if kind == HOST_BUTTONS:
            pending_host.setdefault(value, ns)
        elif kind == sample_kind:
            if value in pending_host:
                host_to_sample.append(ms(pending_host.pop(value), ns))
            pending_samples.append(ns)
        elif kind == PUBLISH:
            publish_at[value] = ns
            waiting_present.extend(pending_samples)
            pending_samples = []
        elif kind == APP_PICTURE:
            if value in publish_at:
                publish_to_picture.append(ms(publish_at[value], ns))
            picture_at = ns
        elif kind == APP_PRESENT_IN:
            present_in_at = ns
        elif kind == APP_PRESENT_OUT:
            if present_in_at is not None:
                present_call.append(ms(present_in_at, ns))
                present_in_at = None
            if picture_at is not None:
                picture_to_present.append(ms(picture_at, ns))
                picture_at = None
                sample_to_present.extend(ms(t, ns) for t in waiting_present)
                waiting_present = []

    span = ms(entries[0][0], entries[-1][0]) / 1000 if entries else 0
    print(f"{len(entries)} marks over {span:.1f} s")
    stats("host read -> game sample", host_to_sample)
    stats("publish -> picture taken", publish_to_picture)
    stats("picture taken -> presented", picture_to_present)
    stats("present call", present_call)
    stats("game sample -> presented", sample_to_present)


if __name__ == "__main__":
    main()
