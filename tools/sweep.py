"""Render every parameter at both ends of its range and fail if any made no
difference.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 152 frames at 120
BPM on the harness's look clip (a ball bouncing), so the last frame is the
one that catches the flash of the beat at 2.5 s: most of the disc's controls
show only in a flash or in motion. Rate acts only with Trigger Rate; Fire is
a press, swept by pressing it on the last frame (`--at`); Threshold acts only
on music (`--kicks`, Trigger Onset, Drive Hand so each onset is a push that
moves the disc); Push and Friction only with Drive Hand;
RPM only with Sync off; Flash only on a disc fast enough for a flash to smear
(Sync off at 600 RPM). An option parameter reads back 0..1 whatever its count
(the fleet's trap), so options are set here by element index. A control whose
ends differ by less than `--floor` (mean 8-bit difference per channel) is
reported as barely alive: vectrix's lesson.

Every render is 320x180 (CI's raster).

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context (keeping only the base), the
addendum's honesty check: what goes dead there is the list of controls that
only act with another one set, which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

# A setting is "Name=Value" (passed as --set) or a list of raw arguments.
MUSIC = [["--kicks"]]
FAST = ["Sync=0", "RPM=1"]

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Trigger": (["Trigger=0"], ["Trigger=6"], []),
    "Rate": (["Rate=0.3"], ["Rate=0.7"], ["Trigger=6"]),
    "Fire": ([], [["--at", "151", "Fire=1"]], ["Trigger=7"]),
    "Threshold": (["Threshold=0"], ["Threshold=1"], MUSIC + ["Trigger=5", "Drive=1"]),
    "Flash": (["Flash=0.2"], ["Flash=0.95"], FAST),
    "Strobe": (["Strobe=0.2"], ["Strobe=0.8"], []),
    "Stations": (["Stations=6"], ["Stations=20"], []),
    "Drive": (["Drive=0"], ["Drive=1"], []),
    "Sync": (["Sync=0"], ["Sync=1"], []),
    "Slip": (["Slip=0.45"], ["Slip=0.55"], []),
    "RPM": (["RPM=0.1"], ["RPM=0.3"], ["Sync=0"]),
    "Push": (["Push=0.3"], ["Push=0.9"], ["Drive=1"]),
    "Friction": (["Friction=0.1"], ["Friction=0.9"], ["Drive=1"]),
    "Print At": (["Print At=0"], ["Print At=0.25"], []),
    "Radius": (["Radius=0.4"], ["Radius=0.7"], []),
    "Card Size": (["Card Size=0.5"], ["Card Size=1"], []),
    "Shutter": (["Shutter=0.3"], ["Shutter=1"], []),
    "Room Light": (["Room Light=0"], ["Room Light=0.5"], []),
    "Persistence": (["Persistence=0"], ["Persistence=1"], []),
    "Background": (["Background=0"], ["Background=1"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
# Audio is the host's FFT buffer: --kicks writes it, and Threshold is swept
# through it.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio"}


def read_png(path):
    """Enough of PNG for zttest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(zttest, out, settings):
    args = [str(zttest), "--out", str(out), "--size", "320x180", "--frames", "152"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"zttest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(zttest):
    result = subprocess.run([str(zttest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"zttest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(zttest, scratch, name, bare):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context)
    tag = f"p{abs(hash(name))}"
    before = render(zttest, scratch / f"{tag}-a.png", context + low)
    after = render(zttest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    zttest = build / "zttest"
    if not zttest.exists():
        print(f"{zttest} not found", file=sys.stderr)
        return 1

    declared = parameters(zttest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    unused = [n for n in SWEEP if n not in declared]
    if unused:
        print(f"the sweep names parameters the plugin does not have: {', '.join(unused)}", file=sys.stderr)
        return 1

    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda n: sweep_one(zttest, scratch, n, args.bare), [n for n in declared if n in SWEEP]))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")

    print(f"{len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
