#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare immutable decoder builds on the same seeded Vail WAV files."""
import argparse
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import time
import zlib
from compare import held_out_cases


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mappings(values):
    return {label: Path(path).resolve() for label, path in
            (value.split("=", 1) for value in values)}


def speed_sweep_cases():
    """Cover every integer speed in the 15..40 WPM operating range."""
    texts = ["CQ CQ DE N0QRS N0QRS PSE K",
             "W7QRS DE JA1XYZ GE OM UR RST 579 QTH TOKYO NAME AKI HW CPY?",
             "R R UR 5NN CO BK TU 5NN WA 73 W7QRS DE N0QRS TU EE"]
    conditions = [("clean", None, 0, 0, []), ("noise", -3, 0, 0, []),
                  ("jitter", 10, .2, 0, []), ("impulse", 10, .03, 0, ["-Q", "2"]),
                  ("fade", 6, .06, .6, []), ("qrm", 10, .03, 0, [])]
    for speed in range(15, 41):
        for name, snr, jitter, fade, extra in conditions:
            for index, text in enumerate(texts):
                pitch = 550 + 83 * ((speed + index) % 5)
                interference = ["-S", f"{pitch + 100}:23:-6:VVV TEST"]
                yield dict(name=f"band_w{speed}_{name}_t{index}", wpm=speed,
                           snr=snr, text=text, pitch=pitch, jitter=jitter,
                           fade=fade, extra=interference if name == "qrm" else extra)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vail", type=Path, required=True)
    parser.add_argument("--vail-build", type=Path)
    parser.add_argument("--decoder", action="append", required=True, help="LABEL=EXECUTABLE")
    parser.add_argument("--library", action="append", default=[], help="LABEL=SHARED_LIBRARY")
    parser.add_argument("--source", action="append", default=[], help="LABEL=SOURCE_ROOT")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--reuse", type=Path, help="Reuse matching binary/input results")
    parser.add_argument("--seeds", type=int, default=4)
    case_set = parser.add_mutually_exclusive_group()
    case_set.add_argument("--held-out", action="store_true")
    case_set.add_argument("--speed-sweep", action="store_true",
                          help="Every integer speed from 15 through 40 WPM, six QSO conditions")
    parser.add_argument("--filter", default="")
    parser.add_argument("--require-better", action="append", default=[],
                        help="Fail unless LABEL beats BASELINE in mean and pooled CER (LABEL=BASELINE)")
    parser.add_argument("--require-not-worse", action="append", default=[],
                        help="Fail unless both mean and pooled CER are <= BASELINE (LABEL=BASELINE)")
    args = parser.parse_args()
    vail = args.vail.resolve()
    build = (args.vail_build or vail / "build").resolve()
    spec = importlib.util.spec_from_file_location("vail_eval", vail / "tools/eval.py")
    grid = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(grid)
    decoders, libraries, roots = map(mappings, (args.decoder, args.library, args.source))
    provenance = {}
    for label, executable in decoders.items():
        row = {"executable": str(executable), "executable_sha256": sha(executable)}
        if label in libraries:
            row.update(library=str(libraries[label]), library_sha256=sha(libraries[label]))
        if label in roots:
            root = roots[label]
            files = sorted((root / "src").glob("*.[ch]"))
            header = root / "include/morsedec.h"
            if header.exists():
                files.append(header)
            row["source_root"] = str(root)
            row["source_sha256"] = {str(p.relative_to(root)): sha(p) for p in files}
        if label in libraries:
            options = libraries[label].parent / 'meson-info/intro-buildoptions.json'
            if options.exists():
                row['build_options'] = {o['name']: o['value'] for o in json.loads(options.read_text())
                                        if o['name'] in ('buildtype', 'optimization', 'b_ndebug',
                                                         'b_sanitize', 'c_args', 'parallel')}
        provenance[label] = row
    prior = json.loads(args.reuse.read_text()) if args.reuse else {}
    previous = {r["case"]["name"]: r for r in prior.get("cases", [])}
    result = {"vail_revision": subprocess.check_output(
        ["git", "-C", str(vail), "rev-parse", "HEAD"], text=True).strip(),
        "generator_sha256": sha(build / "cwgen"), "held_out": args.held_out,
        "speed_sweep": args.speed_sweep,
        "decoders": provenance, "cases": [], "complete": False}
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    base = (list(speed_sweep_cases()) if args.speed_sweep else
            list(held_out_cases()) if args.held_out else grid.cases(False))
    cases = [c for c in grid.with_seeds(base, args.seeds) if args.filter in c["name"]]
    for index, case in enumerate(cases):
        wav = args.cache.resolve() / (case["name"] + ".wav")
        command = [str(build / "cwgen"), "-o", str(wav), "-t", case["text"],
                   "-w", str(case["wpm"]), "-f", str(case["pitch"]),
                   "-j", str(case["jitter"]), "-r",
                   str(zlib.crc32(case["name"].encode()) % 100000)]
        if case["snr"] is not None:
            command += ["-n", str(case["snr"])]
        if case["fade"]:
            command += ["-F", "0.3", "-D", str(case["fade"])]
        command += case.get("extra", [])
        old = previous.get(case['name'], {})
        if not (old.get('case') == case and wav.exists() and
                prior.get('generator_sha256') == result['generator_sha256'] and
                sha(wav) == old.get('sha256')):
            subprocess.run(command, check=True, capture_output=True, timeout=60)
        row = {"case": case, "sha256": sha(wav)}
        for label, executable in decoders.items():
            same_binary = all(provenance[label].get(key) ==
                              prior.get("decoders", {}).get(label, {}).get(key)
                              for key in ("executable_sha256", "library_sha256", "source_sha256"))
            if same_binary and old.get("sha256") == row["sha256"] and old.get("case") == case:
                row[label] = old[label]
                continue
            start = time.perf_counter()
            text = subprocess.run([str(executable), str(wav)], check=True,
                                  capture_output=True, text=True, timeout=60).stdout
            elapsed = time.perf_counter() - start
            ref, got = grid.norm(case["text"]), grid.norm(text)
            errors = grid.lev(ref, got)
            row[label] = {"text": got, "errors": errors, "cer": errors / len(ref),
                          "errors_ns": grid.lev(ref.replace(" ", ""), got.replace(" ", "")),
                          "process_wall_seconds": elapsed}
        result["cases"].append(row)
        if (index + 1) % 20 == 0 or index + 1 == len(cases):
            args.output.write_text(json.dumps(result, indent=2) + "\n")
            print(f"{index + 1}/{len(cases)}", flush=True)
    for label, row in provenance.items():
        if sha(decoders[label]) != row["executable_sha256"]:
            raise RuntimeError(f"{label}: executable changed during comparison")
        if label in libraries and sha(libraries[label]) != row["library_sha256"]:
            raise RuntimeError(f"{label}: library changed during comparison")
        if label in roots and any(sha(roots[label] / p) != digest
                                  for p, digest in row["source_sha256"].items()):
            raise RuntimeError(f"{label}: source changed during comparison")
    result["summary"] = {}
    for label in decoders:
        rows = result["cases"]
        errors = sum(r[label]["errors"] for r in rows)
        characters = sum(len(grid.norm(r["case"]["text"])) for r in rows)
        result["summary"][label] = {
            "cases": len(rows), "errors": errors, "reference_characters": characters,
            "mean_cer": sum(r[label]["cer"] for r in rows) / max(1, len(rows)),
            "pooled_cer": errors / max(1, characters),
            "errors_ns": sum(r[label]["errors_ns"] for r in rows)}
    result["by_wpm"] = {}
    for speed in sorted({row["case"]["wpm"] for row in result["cases"]}):
        rows = [row for row in result["cases"] if row["case"]["wpm"] == speed]
        characters = sum(len(grid.norm(row["case"]["text"])) for row in rows)
        result["by_wpm"][str(speed)] = {
            label: {"cases": len(rows),
                    "mean_cer": sum(row[label]["cer"] for row in rows) / len(rows),
                    "errors": sum(row[label]["errors"] for row in rows),
                    "reference_characters": characters,
                    "pooled_cer": sum(row[label]["errors"] for row in rows) / characters}
            for label in decoders}
    result["complete"] = True
    result["gates"] = []
    for candidate, baseline in (item.split("=", 1) for item in args.require_better):
        a, b = result["summary"][candidate], result["summary"][baseline]
        passed = bool(a["cases"]) and all(a[key] < b[key]
                                         for key in ("mean_cer", "pooled_cer"))
        result["gates"].append({"candidate": candidate, "baseline": baseline,
                                "passed": passed})
    for candidate, baseline in (item.split("=", 1) for item in args.require_not_worse):
        rows = result["cases"]
        # Compare exact rational means; allow no tolerance in edit counts.
        totals = {label: sum((Fraction(row[label]["errors"],
                                      len(grid.norm(row["case"]["text"])))
                              for row in rows), Fraction())
                  for label in (candidate, baseline)}
        passed = bool(rows) and totals[candidate] <= totals[baseline] and (
            result["summary"][candidate]["errors"] <= result["summary"][baseline]["errors"])
        result["gates"].append({"candidate": candidate, "baseline": baseline,
                                "comparison": "<=", "passed": passed})
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result["summary"], indent=2), flush=True)
    if any(not gate["passed"] for gate in result["gates"]):
        raise SystemExit("Accuracy gate failed; see report gates")


if __name__ == "__main__":
    main()
