#!/usr/bin/env python3
"""report.py <run dir> [<older run dir>]: per-group pass rates of a dEQP run (results.tsv), optional
comparison with an older run (fixed / regressed cases)."""
import sys, collections

def load(d):
    r = {}
    for l in open(f"{d}/results.tsv"):
        p = l.rstrip("\n").split("\t")
        if len(p) >= 2:
            r[p[0]] = p[1]
    return r

new = load(sys.argv[1])
tot = collections.Counter(new.values())
ran = sum(v for k, v in tot.items() if k != "Skip")
ok = tot["Pass"] + tot["NotSupported"] + tot["QualityWarning"] + tot["CompatibilityWarning"]
print(f"{len(new)} cases: " + ", ".join(f"{k} {v}" for k, v in tot.most_common()))
print(f"pass rate over executed cases: {ok}/{ran} = {100.0 * ok / max(ran, 1):.1f}% (NotSupported counted as pass, as in Khronos results)\n")
g = collections.defaultdict(collections.Counter)
for c, s in new.items():
    g[".".join(c.split(".")[2:4])][s] += 1
print(f"{'group':48s} {'cases':>6s} {'pass':>6s}  other")
for k in sorted(g):
    c = g[k]; n = sum(c.values()); p = c["Pass"] + c["NotSupported"]
    other = ", ".join(f"{s} {v}" for s, v in c.most_common() if s not in ("Pass", "NotSupported"))
    print(f"{k:48s} {n:6d} {p:6d}  {other}")
if len(sys.argv) > 2:
    old = load(sys.argv[2])
    fixed = [c for c in new if new[c] == "Pass" and old.get(c) not in (None, "Pass")]
    regr = [c for c in new if old.get(c) == "Pass" and new[c] != "Pass"]
    print(f"\nfixed since {sys.argv[2]}: {len(fixed)}; regressed: {len(regr)}")
    for c in regr[:50]:
        print("  REGRESSED", c, old[c], "->", new[c])
