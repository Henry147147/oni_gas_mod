"""Saved-state bounded-energy remap review; no native executable or EOS load."""
from decimal import localcontext
from pathlib import Path
import hashlib
import importlib.util
import json
import sys

spec = importlib.util.spec_from_file_location('swept', Path(__file__).with_name('review_mac_swept_momentum.py'))
swept = importlib.util.module_from_spec(spec)
spec.loader.exec_module(swept)


def review(raw):
    with localcontext() as context:
        context.prec = 80
        return swept.review(swept.parse(raw, bounded_energy=True), bounded_energy=True)


def main():
    raw = Path(sys.argv[1]).read_bytes()
    result = review(raw)
    rejected = []
    for name, tag, at in [('actual-node', 'SWNODE', 5), ('carried-energy', 'SWEVENT', 26),
                          ('compatible-work', 'SWEVENT', 28), ('trace-inventory', 'MASS', 5)]:
        lines = raw.decode('ascii').splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith(tag + ' ') and
                     (tag != 'MASS' or 'e-28' in line or 'e-29' in line))
        row = lines[index].split()
        row[at] = str(float(row[at]) * 1.01 if float(row[at]) else .01)
        lines[index] = ' '.join(row)
        try:
            review(('\n'.join(lines) + '\n').encode('ascii'))
        except (ValueError, KeyError, AssertionError):
            rejected.append(name)
        else:
            raise ValueError('Corrupted saved stream accepted: ' + name)
    lines = raw.decode('ascii').splitlines()
    index = next(i for i, line in enumerate(lines) if line.startswith('SWEVENT ') and
                 int(line.split()[1]) >= 28 and float(line.split()[16]) == 0 and
                 float(line.split()[20]) == 0 and float(line.split()[24]) != 0 and
                 float(line.split()[26]) == 0 and float(line.split()[30]) != 0)
    row = lines[index].split()
    row[30] = row[31] = '0'
    lines[index] = ' '.join(row)
    try:
        review(('\n'.join(lines) + '\n').encode('ascii'))
    except ValueError as error:
        if str(error) != 'Original right pair-work account':
            raise ValueError('Tiny cold-work corruption reached the wrong guard: ' + str(error))
        rejected.append('tiny-stationary-work')
    else:
        raise ValueError('Erased tiny stationary work accepted')
    result.update(InputSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False,
                  AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  SharedReaderSha256=hashlib.sha256(Path(swept.__file__).read_bytes()).hexdigest(),
                  CorruptionsRejected=rejected)
    with Path(sys.argv[2]).open('x') as out:
        json.dump(result, out, indent=2)
        out.write('\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
