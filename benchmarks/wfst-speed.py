#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Alternate preloaded-PCM CPU benchmarks; construction and I/O are untimed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mappings(values):
    return {label: Path(path).resolve() for label, path in
            (value.split("=", 1) for value in values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", action="append", required=True, help="LABEL=SPEED_EXECUTABLE")
    parser.add_argument("--library", action="append", default=[], help="LABEL=SHARED_LIBRARY")
    parser.add_argument("--audio", action="append", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--cpu", type=int, help="Linux CPU affinity for all measured processes")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error("--rounds must be positive")
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    decoders, libraries = mappings(args.decoder), mappings(args.library)
    files = list(decoders.values()) + list(libraries.values()) + args.audio
    hashes = {str(p): sha(p) for p in files}
    result = {"platform": platform.platform(),
              "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
              "compiler": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0],
              "decoders": {label: {"executable": str(executable),
                                   "library": str(libraries[label]) if label in libraries else None}
                           for label, executable in decoders.items()},
              "sha256": hashes, "runs": [], "complete": False}
    for index in range(args.rounds):
        order = list(decoders) if index % 2 == 0 else list(decoders)[::-1]
        for audio in args.audio:
            for label in order:
                row = json.loads(subprocess.check_output([str(decoders[label]), str(audio)], text=True))
                if not row["cpu_seconds"] or any(t <= 0 for t in row["cpu_seconds"]):
                    raise ValueError("Invalid CPU measurement")
                row.update(label=label, round=index, audio=str(audio))
                result["runs"].append(row)
    if any(sha(Path(path)) != digest for path, digest in hashes.items()):
        raise RuntimeError("Input or decoder changed during measurement")
    result["summary"] = {}
    for audio in args.audio:
        result["summary"][str(audio)] = {}
        for label in decoders:
            rows = [r for r in result["runs"] if r["audio"] == str(audio) and r["label"] == label]
            samples = [t for row in rows for t in row["cpu_seconds"]]
            median = statistics.median(samples)
            result["summary"][str(audio)][label] = {
                "samples": len(samples), "median_cpu_seconds": median,
                "min_cpu_seconds": min(samples), "max_cpu_seconds": max(samples),
                "audio_seconds": rows[0]["audio_seconds"],
                "realtime_factor": rows[0]["audio_seconds"] / median}
    result["complete"] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
