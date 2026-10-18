#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure fixed-decoder ham QSO CER curves and sustained SNR thresholds."""
import argparse
import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import time
import wave
import zlib

TEXTS = ["CQ CQ DE N0QRS N0QRS PSE K",
         "W7QRS DE JA1XYZ GE OM UR RST 579 QTH TOKYO NAME AKI HW CPY?",
         "R R UR 5NN CO BK TU 5NN WA 73 W7QRS DE N0QRS TU EE"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mappings(values):
    return dict((label, Path(path).resolve())
                for label, path in (value.split("=", 1) for value in values))


def summarize(rows):
    characters = sum(len(row["reference"]) for row in rows)
    errors = sum(row["errors"] for row in rows)
    cers = sorted(row["cer"] for row in rows)
    return dict(cases=len(rows), reference_characters=characters, errors=errors,
                mean_cer=sum(cers) / len(cers), pooled_cer=errors / characters,
                errors_ns=sum(row["errors_ns"] for row in rows),
                exact_cases=sum(row["errors"] == 0 for row in rows),
                empty_cases=sum(not row["text"] for row in rows),
                worst_cer=max(cers),
                clipped_fraction=sum(row["clipped_samples"] for row in rows) /
                sum(row["samples"] for row in rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vail", type=Path, required=True)
    parser.add_argument("--generator", action="append", required=True,
                        help="LABEL=CWGEN_EXECUTABLE")
    parser.add_argument("--generator-source", action="append", default=[],
                        help="LABEL=CWGEN_SOURCE")
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path("."))
    parser.add_argument("--speeds", default=",".join(map(str, range(15, 41))))
    parser.add_argument("--snrs", default="3,0,-1,-2,-3,-4,-5,-6,-7,-8,-9,-10,-11,-12")
    parser.add_argument("--seeds", type=int, default=4)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reuse", type=Path)
    args = parser.parse_args()
    speeds = sorted(set(map(int, args.speeds.split(','))))
    snrs = sorted(set(map(float, args.snrs.split(','))), reverse=True)
    if not speeds or min(speeds) <= 0 or args.seeds < 1 or args.jobs < 1:
        parser.error("Speeds, seeds and jobs must be positive")
    generators = mappings(args.generator)
    gen_sources = mappings(args.generator_source)
    decoder, library, root = (p.resolve() for p in
                              (args.decoder, args.library, args.source))
    spec = importlib.util.spec_from_file_location("vail_eval", args.vail / "tools/eval.py")
    grid = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(grid)
    sources = sorted((root / 'src').glob('*.[ch]')) + [root / 'include/morsedec.h']
    provenance = dict(executable=str(decoder), executable_sha256=sha(decoder),
                      library=str(library), library_sha256=sha(library),
                      source_root=str(root),
                      source_sha256={str(p.relative_to(root)): sha(p) for p in sources})
    options = library.parent / 'meson-info/intro-buildoptions.json'
    if options.exists():
        provenance['build_options'] = {o['name']: o['value'] for o in json.loads(options.read_text())
                                       if o['name'] in ('buildtype', 'optimization', 'b_ndebug',
                                                        'b_sanitize', 'c_args', 'parallel')}
    gen_info = {label: dict(executable=str(path), executable_sha256=sha(path))
                for label, path in generators.items()}
    for label, path in gen_sources.items():
        gen_info[label].update(source=str(path), source_sha256=sha(path))
    old = json.loads(args.reuse.read_text()) if args.reuse else {}
    previous = {row['id']: row for row in old.get('cases', [])}
    result = dict(complete=False, runner_sha256=sha(Path(__file__)),
                  vail_revision=subprocess.check_output(
                      ['git', '-C', str(args.vail), 'rev-parse', 'HEAD'], text=True).strip(),
                  decoder=provenance, generators=gen_info,
                  method='Default automatic channel selection, final snapshot after four seconds '
                         'of silence and md_flush; uppercase and collapse whitespace; '
                         'spaces, punctuation and O/0 count as edits',
                  snr_reference='Nominal key-down sine power / Gaussian noise power in 2500 Hz '
                                'before PCM clipping, as specified by Vail cwgen',
                  paired_noise='CRC32 seed depends on WPM, message and repeat; '
                               'the same noise realization is used across SNR and generator modes',
                  speeds=speeds, snrs=snrs, seeds=args.seeds, cases=[])
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tasks = []
    for speed in speeds:
        for snr in snrs:
            for index, text in enumerate(TEXTS):
                for repeat in range(args.seeds):
                    name = f'sensitivity_w{speed}_t{index}_r{repeat}'
                    seed = zlib.crc32(name.encode()) % 100000
                    case = dict(wpm=speed, snr=snr, message=index, reference=grid.norm(text),
                                pitch=550 + 83 * ((speed + index) % 5), seed=seed,
                                repeat=repeat, jitter=0, fade=0)
                    for label in generators:
                        tasks.append((label, f'{label}_{name}_s{snr:g}', case))

    def run(task):
        label, name, case = task
        prior = previous.get(name)
        if (prior and prior['case'] == case and old.get('runner_sha256') == result['runner_sha256'] and
                old.get('decoder') == provenance and
                old.get('generators', {}).get(label) == gen_info[label]):
            return prior
        wav = args.cache / (name + '.wav')
        command = [str(generators[label]), '-o', str(wav), '-t', case['reference'],
                   '-w', str(case['wpm']), '-f', str(case['pitch']),
                   '-n', str(case['snr']), '-j', '0', '-r', str(case['seed'])]
        subprocess.run(command, check=True, capture_output=True, timeout=60)
        digest = sha(wav)
        with wave.open(str(wav)) as audio:
            if audio.getsampwidth() != 2 or audio.getnchannels() != 1:
                raise ValueError('Expected 16-bit mono WAV')
            samples = array.array('h', audio.readframes(audio.getnframes()))
            if sys.byteorder != 'little':
                samples.byteswap()
            clipped = sum(x in (-32768, 32767) for x in samples)
        start = time.perf_counter()
        got = grid.norm(subprocess.check_output([str(decoder), str(wav)], text=True, timeout=60))
        elapsed = time.perf_counter() - start
        reference = case['reference']
        errors = grid.lev(reference, got)
        return dict(id=name, generator=label, case=case, reference=reference, sha256=digest,
                    text=got, errors=errors, cer=errors / len(reference),
                    errors_ns=grid.lev(reference.replace(' ', ''), got.replace(' ', '')),
                    samples=len(samples), clipped_samples=clipped,
                    process_wall_seconds=elapsed)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for index, row in enumerate(pool.map(run, tasks), 1):
            result['cases'].append(row)
            if index % 100 == 0 or index == len(tasks):
                args.output.write_text(json.dumps(result, indent=2) + '\n')
                print(f'{index}/{len(tasks)}', flush=True)
    if sha(decoder) != provenance['executable_sha256'] or sha(library) != provenance['library_sha256']:
        raise RuntimeError('Measured decoder changed')
    for path, digest in provenance['source_sha256'].items():
        if sha(root / path) != digest:
            raise RuntimeError('Measured source changed: ' + path)
    for label, path in generators.items():
        if sha(path) != gen_info[label]['executable_sha256']:
            raise RuntimeError('Generator changed: ' + label)
    for label, path in gen_sources.items():
        if sha(path) != gen_info[label]['source_sha256']:
            raise RuntimeError('Generator source changed: ' + label)
    if sha(Path(__file__)) != result['runner_sha256']:
        raise RuntimeError('Runner changed')
    result['curves'] = {}
    result['thresholds'] = {}
    for label in generators:
        curves, thresholds = {}, {}
        for speed in speeds:
            rows = [r for r in result['cases'] if r['generator'] == label and r['case']['wpm'] == speed]
            curve = {str(snr): summarize([r for r in rows if r['case']['snr'] == snr]) for snr in snrs}
            curves[str(speed)] = curve
            limits = {}
            for limit in [.01, .05, .10]:
                sustained = []
                passing = [snr for snr in snrs if curve[str(snr)]['mean_cer'] <= limit]
                for snr in snrs:
                    if curve[str(snr)]['mean_cer'] > limit:
                        break
                    sustained.append(snr)
                limits[str(limit)] = dict(
                    lowest_passing_snr=min(passing) if passing else None,
                    sustained_snr=min(sustained) if sustained else None,
                    lower_bound_reached=bool(sustained and min(sustained) == min(snrs)),
                    nonmonotonic_pass=bool(passing and (not sustained or min(passing) < min(sustained))))
            thresholds[str(speed)] = limits
        result['curves'][label] = curves
        result['thresholds'][label] = thresholds
    result['threshold_method'] = (
        'Sustained threshold: lowest tested SNR whose mean CER and every '
        'higher tested SNR are <= the stated limit; no interpolation')
    result['complete'] = True
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for label, speeds in result['thresholds'].items():
        print(label, {speed: limits['0.05']['sustained_snr']
                      for speed, limits in speeds.items()}, flush=True)


if __name__ == '__main__':
    main()
