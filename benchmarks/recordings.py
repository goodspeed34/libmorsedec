#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Re-evaluate archived PCM WAV recordings using chronological snapshots."""
import argparse
import array
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
import wave

class Config(C.Structure):
    _fields_ = [("sample_rate", C.c_uint32), ("fft_size", C.c_size_t),
                ("hop_size", C.c_size_t), ("min_frequency", C.c_double),
                ("max_frequency", C.c_double), ("max_channels", C.c_size_t),
                ("initial_wpm", C.c_double)]


class Geometry(C.Structure):
    _fields_ = [("sample_rate", C.c_uint32), ("fft_size", C.c_size_t),
                ("hop_size", C.c_size_t), ("spectrum_size", C.c_size_t),
                ("first_frequency", C.c_double), ("bin_spacing", C.c_double),
                ("frame_seconds", C.c_double)]


class Character(C.Structure):
    _fields_ = [("character", C.c_ubyte), ("word_type", C.c_int),
                ("ordinal", C.c_uint64), ("low_snr", C.c_int),
                ("word_space_probability", C.c_double), ("word_end", C.c_int)]


def distance(a, b):
    row = list(range(len(b) + 1))
    for i, x in enumerate(a, 1):
        new = [i]
        for j, y in enumerate(b, 1):
            new.append(min(new[-1] + 1, row[j] + 1, row[j - 1] + (x != y)))
        row = new
    return row[-1]


def decode(lib, path):
    with wave.open(str(path)) as wav:
        if wav.getsampwidth() != 2:
            raise ValueError("Expected signed 16-bit PCM WAV")
        config, geometry = Config(), Geometry()
        lib.md_config_init(C.byref(config))
        config.sample_rate = wav.getframerate()
        decoder = lib.md_new(C.byref(config))
        if not decoder:
            raise RuntimeError("Decoder creation failed")
        records = {}

        def collect():
            chars, count = C.POINTER(Character)(), C.c_size_t()
            if not lib.md_get_characters(decoder, 0, C.byref(chars), C.byref(count)):
                raise RuntimeError("Snapshot query failed")
            for i in range(count.value):
                ch = chars[i]
                records[ch.ordinal] = chr(ch.character) + " " * ch.word_end

        try:
            assert lib.md_get_geometry(decoder, C.byref(geometry))
            buf = (C.c_float * geometry.hop_size)()
            while data := wav.readframes(geometry.hop_size):
                pcm = array.array("h", data)
                if sys.byteorder != "little":
                    pcm.byteswap()
                channels = wav.getnchannels()
                count = len(pcm) // channels
                for i in range(count):
                    buf[i] = sum(pcm[i * channels:(i + 1) * channels]) / channels / 32768
                if not lib.md_process(decoder, buf, count):
                    raise RuntimeError("Processing failed")
                collect()
            assert lib.md_flush(decoder)
            collect()
        finally:
            lib.md_delete(decoder)
    return "".join(records[k] for k in sorted(records))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--references", type=Path,
                        default=Path("docs/benchmarks/corpus-after.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path,
                        help="Fail if pooled CER exceeds this identical-input report")
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    signatures = {
        "config_init": ([C.POINTER(Config)], None),
        "new": ([C.POINTER(Config)], C.c_void_p),
        "delete": ([C.c_void_p], None),
        "process": ([C.c_void_p, C.POINTER(C.c_float), C.c_size_t], C.c_int),
        "flush": ([C.c_void_p], C.c_int),
        "get_geometry": ([C.c_void_p, C.POINTER(Geometry)], C.c_int),
        "get_characters": ([C.c_void_p, C.c_uint64, C.POINTER(C.POINTER(Character)),
                            C.POINTER(C.c_size_t)], C.c_int),
    }
    for name, (types, result) in signatures.items():
        fn = getattr(lib, "md_" + name)
        fn.argtypes, fn.restype = types, result
    report = {"library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
              "method": "current-channel snapshots by ordinal; uppercase; remove whitespace",
              "cases": []}
    for case in json.loads(args.references.read_text())["cases"]:
        path = args.audio / (case["id"] + ".wav")
        text = decode(lib, path)
        ref, got = ("".join(s.upper().split()) for s in (case["reference"], text))
        row = dict(id=case["id"], reference=case["reference"], decoded=text,
                   wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                   errors=distance(ref, got), reference_characters=len(ref))
        report["cases"].append(row)
        print(row["id"], row["errors"], "/", len(ref))
    report["total_errors"] = sum(c["errors"] for c in report["cases"])
    report["reference_characters"] = sum(c["reference_characters"] for c in report["cases"])
    if hashlib.sha256(args.library.read_bytes()).hexdigest() != report["library_sha256"]:
        raise RuntimeError("Measured library changed")
    if args.baseline:
        baseline = json.loads(args.baseline.read_text())
        identity = lambda r: (r["id"], r["reference"], r["wav_sha256"],
                              r["reference_characters"])
        if ([identity(r) for r in baseline["cases"]] !=
                [identity(r) for r in report["cases"]]):
            raise RuntimeError("Baseline recording inputs/references differ")
        report["baseline_report"] = str(args.baseline)
        report["baseline_sha256"] = hashlib.sha256(args.baseline.read_bytes()).hexdigest()
        report["baseline_errors"] = baseline["total_errors"]
        report["gate_passed"] = bool(report["cases"]) and (
            report["total_errors"] <= baseline["total_errors"])
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if not report.get("gate_passed", True):
        raise SystemExit("Recording CER gate failed; see report")


if __name__ == "__main__":
    main()
