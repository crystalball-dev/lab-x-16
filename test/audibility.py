"""How much does each preset change what a mixer track carries? For every preset: the difference between the processed
render and the dry one (bypassed, latency-aligned), relative to the dry, in dB, and the loudness change in brackets, on
three sources: the suite's drums, a sustained pad (a saw chord with a slow attack) and a bass line.

The T-tests check that each mechanism works on signals made for it; this checks that an effect is heard. As a guide
from the first listening round (docs/DESIGN.md): effects heard plainly sat between -10 and +14 dB; -15 dB and below
read as subtle or as nothing at all.

Usage:  python -I test/audibility.py <slug> [<slug> ...]   (renders go to test/out/audibility, git-ignored)
"""

import pathlib
import sys

import numpy as np
import soundfile as sf

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "test" / "out" / "audibility"
sys.path.insert(0, str(ROOT / "test"))
sys.path.insert(0, str(ROOT / "test" / "python"))
import run_tests as rt  # noqa: E402

SR = 48000
SECONDS = 8


def sources():
    n = SECONDS * SR
    t = np.arange(n) / SR
    pad = np.zeros(n)
    for f in (130.81, 164.81, 196.0, 261.63):
        ph = np.cumsum(np.full(n, f / SR)) % 1.0
        pad += (2 * ph - 1) * 0.12
    pad *= np.minimum(1, t / 0.4) * np.minimum(1, (SECONDS - t) / 0.5)
    bass = np.zeros(n)
    notes, step = (55.0, 55.0, 65.41, 73.42), SR // 2
    for i in range(0, n, step):
        k = np.arange(min(step, n - i))
        ph = (notes[(i // step) % 4] / SR * k) % 1.0
        bass[i:i + len(k)] = (2 * ph - 1) * 0.35 * np.exp(-k / SR * 3.0) * np.minimum(1, k / 100)
    files = {}
    for name, x in (("pad", pad), ("bass", bass)):
        path = OUT / f"src_{name}.wav"
        sf.write(str(path), np.stack([x, x], 1), SR, subtype="FLOAT")
        files[name] = path
    return files


def render(h, name, args):
    wav = OUT / f"{h.slug}_{name}.wav"
    if h.run(["--out", wav, "--seconds", str(SECONDS)] + args)["exit"] != 0:
        return None
    data, _ = sf.read(str(wav), always_2d=True)
    return data


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    OUT.mkdir(parents=True, exist_ok=True)
    src = sources()
    known = rt.effects()
    for slug in sys.argv[1:]:
        h = rt.Harness(slug, known[slug])
        presets = h.params()
        print(f"== {slug}")
        for label, args in (("drums", ["--signal", "drums:120"]), ("pad", ["--in", src["pad"], "--level", "0"]),
                            ("bass", ["--in", src["bass"], "--level", "0"])):
            dry = render(h, f"{label}_dry", args + ["--set", "bypass=1"])
            cells = []
            for p, name in enumerate(presets):
                out = render(h, f"{label}_p{p}", args + ["--program", str(p)])
                if out is None or dry is None:
                    cells.append(f"{name[:18]:18s}    ERR")
                    continue
                diff = 10 * np.log10((np.sum((out - dry) ** 2) + 1e-20) / (np.sum(dry ** 2) + 1e-20))
                loud = 10 * np.log10((np.sum(out ** 2) + 1e-20) / (np.sum(dry ** 2) + 1e-20))
                cells.append(f"{name[:18]:18s} {diff:6.1f} ({loud:+5.1f})")
            print(f"  {label:5s} " + " | ".join(cells))


if __name__ == "__main__":
    main()
