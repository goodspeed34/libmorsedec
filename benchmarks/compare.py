#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run both decoders on Vail's exact seeded grid; retain per-case evidence.

Requires a separately built Vail checkout, not vendored source. See README.md.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import time
import zlib


def held_out_cases():
    """Different messages, speeds and pitches from the published tuning grid."""
    texts = ["CQ DE W7QRS W7QRS PSE K",
             "UR RST 579 NAME ALICE QTH OSLO TEMP 8C 73",
             "PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS 8062149753"]
    for speed in (14, 19, 27, 34, 39):
        for snr in (None, 8, 2, -2):
            for i, text in enumerate(texts):
                yield dict(name=f"hold_w{speed}_s{snr}_t{i}", wpm=speed, snr=snr,
                           text=text, pitch=543 + i * 137, jitter=0.06,
                           fade=0.4 if i == 1 else 0.0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vail", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", type=int, default=4)
    parser.add_argument("--filter", default="")
    parser.add_argument("--chunk", type=int)
    parser.add_argument("--held-out", action="store_true")
    parser.add_argument("--library", type=Path,
                        help="Library loaded by the adapter (for checksum provenance)")
    args = parser.parse_args()
    vail = args.vail.resolve()
    spec = importlib.util.spec_from_file_location("vail_eval", vail / "tools/eval.py")
    grid = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(grid)
    library = args.library or args.decoder.resolve().parent / "libmorsedec.so"
    library_hash = hashlib.sha256(library.read_bytes()).hexdigest()
    root = Path(__file__).resolve().parents[1]
    sources = sorted((root / "src").glob("*.[ch]")) + [root / "include/morsedec.h"]
    source_hashes = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sources}
    cache = args.output.parent / (args.output.stem + "-audio")
    cache.mkdir(parents=True, exist_ok=True)
    rows = []
    cases = list(held_out_cases()) if args.held_out else grid.cases(False)
    for case in grid.with_seeds(cases, args.seeds):
        if args.filter not in case["name"]:
            continue
        wav = cache / (case["name"] + ".wav")
        command = [str(vail / "build/cwgen"), "-o", str(wav), "-t", case["text"],
                   "-w", str(case["wpm"]), "-f", str(case["pitch"]),
                   "-j", str(case["jitter"]), "-r",
                   str(zlib.crc32(case["name"].encode()) % 100000)]
        if case["snr"] is not None:
            command += ["-n", str(case["snr"])]
        if case["fade"]:
            command += ["-F", "0.3", "-D", str(case["fade"])]
        command += case.get("extra", [])
        subprocess.run(command, check=True, capture_output=True)
        row = dict(case=case, sha256=hashlib.sha256(wav.read_bytes()).hexdigest())
        for label, executable in [("vail", vail / "build/vail_decode"),
                                  ("morsedec", args.decoder.resolve())]:
            cmd = [str(executable), str(wav)]
            if label == "morsedec" and args.chunk:
                cmd += [str(args.chunk)]
            start = time.perf_counter()
            text = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
            seconds = time.perf_counter() - start
            ref, got = grid.norm(case["text"]), grid.norm(text)
            row[label] = dict(text=got, errors=grid.lev(ref, got),
                              cer=grid.lev(ref, got) / len(ref), seconds=seconds,
                              errors_ns=grid.lev(ref.replace(" ", ""), got.replace(" ", "")))
        rows.append(row)
    if hashlib.sha256(library.read_bytes()).hexdigest() != library_hash:
        raise RuntimeError("Decoder library changed during comparison")
    if any(hashlib.sha256((root / p).read_bytes()).hexdigest() != digest
           for p, digest in source_hashes.items()):
        raise RuntimeError("Decoder source changed during comparison")
    result = dict(vail_revision=subprocess.check_output(
        ["git", "-C", str(vail), "rev-parse", "HEAD"], text=True).strip(),
        decoder_sha256=hashlib.sha256(args.decoder.read_bytes()).hexdigest(),
        library_sha256=library_hash, held_out=args.held_out, chunk=args.chunk,
        source_sha256=source_hashes,
        cases=rows)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for label in ("vail", "morsedec"):
        print(label, len(rows), "cases; mean CER",
              100 * sum(r[label]["cer"] for r in rows) / max(1, len(rows)))


if __name__ == "__main__":
    main()
