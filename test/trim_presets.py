"""Loudness trims for an effect's presets (docs/EFFECTS.md section 0.2): each preset's `output` is set so the RMS of
the reference signal (synthesized drums, 120 BPM, -12 dBFS) comes out within 1 dB of the input at the preset's own mix.

Usage:  python -I test/trim_presets.py --plugin <slug> [--write]
Measures every preset except Init at OUTPUT 0 dB and prints the trim; --write puts { "output", trim } into the
preset table in plugins/<slug>/*.cpp (replacing an existing output entry). OUTPUT is applied after the chain, so the
trim is exact in one pass.
"""

import argparse
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_tests as rt  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    info = rt.effects()[args.plugin]
    h = rt.Harness(args.plugin, info)
    presets = h.params()
    signal = ["--signal", "drums:120", "--seconds", "8"]
    _, ref = h.render("trim_input", signal + rt.sets("bypass=1"))
    ref_db = float(ref["rms_db"])
    trims = {}
    for p, name in enumerate(presets):
        if p == 0:
            continue
        _, st = h.render(f"trim_preset{p}", signal + ["--program", str(p)] + rt.sets("output=0"))
        trims[name] = round(ref_db - float(st["rms_db"]), 1) + 0.0   # + 0.0: no "-0.0"
        print(f"{name:28s} trim {trims[name]:+.1f} dB")

    if not args.write:
        return 0
    sources = sorted((rt.ROOT / "plugins" / args.plugin).glob("*.cpp"))
    path = sources[0]
    text = path.read_text(encoding="utf-8")
    for name, trim in trims.items():
        start = text.find('{ "' + name + '", {')
        if start < 0:
            print(f"preset {name} not found in {path.name}")
            return 1
        # From the preset's first value to its last: the list closes at the first "} }" after the opening.
        body_start = text.index("{", text.index("{", start + 1) + 1)
        end = text.index("} }", body_start) + 1
        body = text[body_start:end]
        value = f'{{ "output", {trim:.1f}f }}'
        if '"output"' in body:
            body = re.sub(r'\{ "output", -?[0-9.]+f \}', value, body)
        else:
            body = body.rstrip() + ", " + value
        text = text[:body_start] + body + text[end:]
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {len(trims)} trims to {path.relative_to(rt.ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
