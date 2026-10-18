#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay cached WAVs against archived CER reports; fail on aggregate regression.

Run from the repository root. Cache directories follow the commands in
benchmarks/README.md. Baselines are immutable docs/benchmarks reports. An
append-only checkpoint avoids repeating identical measurements after interruption.
"""

from fractions import Fraction
import sys
import argparse, concurrent.futures as F, gzip, hashlib, json, subprocess, time
from pathlib import Path


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def norm(s):
    return " ".join(s.upper().split())


def lev(a, b):
    row = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        nxt = [i]
        for j, y in enumerate(b, 1):
            nxt.append(min(nxt[-1] + 1, row[j] + 1, row[j - 1] + (x != y)))
        row = nxt
    return row[-1]


def summary(rows, key):
    n = sum(len(r["reference"]) for r in rows)
    e = sum(r[key]["errors"] for r in rows)
    return dict(
        cases=len(rows),
        errors=e,
        reference_characters=n,
        mean_cer=sum(r[key]["cer"] for r in rows) / len(rows),
        pooled_cer=e / n,
        errors_ns=sum(r[key]["errors_ns"] for r in rows),
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--decoder", type=Path, required=True)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--sets", default="qso,grid,additional")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--filter", default="")
    a = p.parse_args()
    prov = dict(
        executable_sha256=sha(a.decoder),
        library_sha256=sha(a.library),
        source_sha256={
            str(x.relative_to(a.source)): sha(x)
            for x in sorted(a.source.glob("src/*.[ch]"))
            + [a.source / "include/morsedec.h"]
        },
    )
    tasks = []
    reports = {}
    for name in a.sets.split(","):
        if name.startswith("snr-"):
            basefile = Path("docs/benchmarks/wfst-sensitivity-" + name[4:] + ".json.gz")
            base = json.loads(gzip.decompress(basefile.read_bytes()))
            cache = Path("build-wfst-sensitivity/audio")
            oldrows = [
                dict(
                    id=r["id"],
                    case=r["case"],
                    sha256=r["sha256"],
                    reference=r["reference"],
                    baseline={k: r[k] for k in ("text", "errors", "cer", "errors_ns")},
                )
                for r in base["cases"]
            ]
            oldprov = base["decoder"]
        else:
            basefile = Path("docs/benchmarks/wfst-ham-" + name + ".json")
            base = json.loads(basefile.read_text())
            cache = Path(
                "build-wfst-range/audio"
                if name == "qso"
                else (
                    "build-wfst-bench/held-out-audio"
                    if name == "additional"
                    else "build-wfst-bench/audio"
                )
            )
            oldrows = [
                dict(
                    id=r["case"]["name"],
                    case=r["case"],
                    sha256=r["sha256"],
                    reference=norm(r["case"]["text"]),
                    baseline=r["wfst"],
                )
                for r in base["cases"]
            ]
            oldprov = base["decoders"]["wfst"]
        rows = [r for r in oldrows if a.filter in r["id"]]
        reports[name] = dict(
            complete=False,
            baseline_report=str(basefile),
            baseline_sha256=sha(basefile),
            baseline_decoder=oldprov,
            candidate=prov,
            cases=[],
            expected_cases=len(oldrows),
            runner_sha256=sha(__file__),
        )
        for r in rows:
            tasks.append((name, cache / (r["id"] + ".wav"), r))
    a.output.mkdir(parents=True, exist_ok=True)
    checkpoint = a.output / "checkpoint.jsonl"
    identity = hashlib.sha256(json.dumps(prov, sort_keys=True).encode()).hexdigest()
    saved = {}
    if checkpoint.exists():
        for line in checkpoint.read_text().splitlines():
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue
            if entry["identity"] == identity:
                saved[tuple(entry["key"])] = entry["candidate"]

    def run(task):
        name, wav, r = task
        if sha(wav) != r["sha256"]:
            raise RuntimeError("WAV differs from baseline: " + str(wav))
        start = time.monotonic()
        result = subprocess.run(
            [str(a.decoder.resolve()), str(wav.resolve())],
            capture_output=True,
            text=True,
            timeout=60,
        )
        got = norm(result.stdout)
        ref = r["reference"]
        errors = lev(ref, got)
        r["candidate"] = dict(
            text=got,
            errors=errors,
            cer=errors / len(ref),
            errors_ns=lev(ref.replace(" ", ""), got.replace(" ", "")),
            returncode=result.returncode,
            stderr=result.stderr,
            wall_seconds=time.monotonic() - start,
        )
        return name, r

    unique = {}
    for name, wav, row in tasks:
        key = (str(wav.resolve()), row["sha256"], row["reference"])
        unique.setdefault(key, (name, wav, row))
    measured = {}
    pending = {}
    for key, task in unique.items():
        if sha(task[1]) != task[2]["sha256"]:
            raise RuntimeError("WAV differs from baseline: " + str(task[1]))
        if key in saved:
            measured[key] = saved[key]
        else:
            pending[key] = task
    with checkpoint.open("a") as log, F.ThreadPoolExecutor(a.jobs) as pool:
        for i, (key, result) in enumerate(
            zip(pending, pool.map(run, pending.values())), 1
        ):
            measured[key] = result[1]["candidate"]
            log.write(
                json.dumps(dict(identity=identity, key=key, candidate=measured[key]))
                + "\n"
            )
            log.flush()
            if i % 100 == 0:
                print(len(measured), "/", len(unique), flush=True)
    for name, wav, row in tasks:
        row["candidate"] = measured[
            (str(wav.resolve()), row["sha256"], row["reference"])
        ]
        reports[name]["cases"].append(row)
    if (
        sha(a.decoder) != prov["executable_sha256"]
        or sha(a.library) != prov["library_sha256"]
    ):
        raise RuntimeError("Binary changed")
    if any(sha(a.source / x) != v for x, v in prov["source_sha256"].items()):
        raise RuntimeError("Source changed")
    for name, d in reports.items():
        if not d["cases"]:
            continue
        d["summary"] = {k: summary(d["cases"], k) for k in ("baseline", "candidate")}
        d["complete"] = len(d["cases"]) == d["expected_cases"]
        d["gate_passed"] = (
            d["complete"]
            and d["summary"]["candidate"]["errors"]
            <= d["summary"]["baseline"]["errors"]
            and sum(
                Fraction(r["candidate"]["errors"], len(r["reference"]))
                for r in d["cases"]
            )
            <= sum(
                Fraction(r["baseline"]["errors"], len(r["reference"]))
                for r in d["cases"]
            )
            and not any(r["candidate"]["returncode"] for r in d["cases"])
        )
        d["changed_cases"] = sum(
            r["baseline"]["errors"] != r["candidate"]["errors"] for r in d["cases"]
        )
        (a.output / (name + ".json")).write_text(json.dumps(d, indent=2) + "\n")
        print(
            name,
            json.dumps(d["summary"]),
            "PASS" if d["gate_passed"] else "FAIL",
            flush=True,
        )
    sys.exit(0 if all(d.get("gate_passed", False) for d in reports.values()) else 1)


if __name__ == "__main__":
    main()
