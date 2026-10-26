#!/usr/bin/env python3
"""Run a dEQP case list on the Xbox 360 over SSH, surviving GPU hangs.

Batches of cases run on the console (deqp binary + data on the NFS root at /opt/deqp).
Results (.qpa) land on NFS, so they are parsed here directly.  A batch that times out, or
after which the kernel reports a missed fence / hung GPU, has its first non-passing case
(or the case left running) marked Hang; the console is IR power-cycled and the remaining
cases are re-queued.  State is kept in <out>/results.tsv so a run resumes where it stopped.

usage: deqp-run.py <caselist> <outdir> [--batch N] [--filter REGEX] [--limit N]
"""
import argparse, os, re, subprocess, sys, time, collections

NFS = os.environ.get("XBOX_NFS_ROOT", "/srv/nfs/xenon")   # the console's NFS root, as seen from the host
DEQP = "/opt/deqp"
SSH = ["ssh", "-o", "ConnectTimeout=5", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=5",
       "-o", "ServerAliveCountMax=3"] + os.environ.get("XBOX_SSH_OPTS", "").split() + [
       "root@" + os.environ.get("XBOX_HOST", "10.0.0.206")]
CYCLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "irblaster", "xbox-cycle")
PASSLIKE = {"Pass", "NotSupported", "QualityWarning", "CompatibilityWarning"}

def log(msg, out):
    line = time.strftime("%F %T ") + msg
    print(line, flush=True)
    with open(os.path.join(out, "run.log"), "a") as f:
        f.write(line + "\n")

def ssh(cmd, timeout):
    try:
        r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return None, "timeout"

def gpu_state():
    rc, o = ssh("cat /sys/module/xegpu/parameters/gpu_hung; cat /sys/module/xegpu/parameters/fence_timeouts || echo 1", 15)
    if rc is None or rc not in (0, 1):
        return None
    v = o.split()
    try:
        return (v[0] == "Y", int(v[1]))
    except (IndexError, ValueError):
        return None

def parse_qpa(path):
    """-> (ordered list of (case, status, detail), name of case begun but never ended)"""
    res, cur = [], None
    if not os.path.exists(path):
        return res, None
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("#beginTestCaseResult "):
                cur, status, detail = line.split(None, 1)[1].strip(), None, ""
            elif line.startswith("#terminateTestCaseResult ") and cur:
                res.append((cur, "Terminated", line.split(None, 1)[1].strip())); cur = None
            elif line.startswith("#endTestCaseResult") and cur:
                res.append((cur, status or "Unknown", detail)); cur = None
            elif cur and "<Result StatusCode=" in line:
                m = re.search(r'StatusCode="([^"]+)">([^<]*)', line)
                if m:
                    status, detail = m.group(1), m.group(2)
    return res, cur

def load_done(out):
    done = collections.OrderedDict()
    p = os.path.join(out, "results.tsv")
    if os.path.exists(p):
        for line in open(p):
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                done[parts[0]] = parts[1]
    return done

def record(out, case, status, detail):
    with open(os.path.join(out, "results.tsv"), "a") as f:
        f.write(f"{case}\t{status}\t{detail.replace(chr(9), ' ')[:200]}\n")

def summary(out, cases):
    done = load_done(out)
    groups = collections.defaultdict(collections.Counter)
    tot = collections.Counter()
    for c in cases:
        if c in done:
            g = ".".join(c.split(".")[:3])
            groups[g][done[c]] += 1
            tot[done[c]] += 1
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write(f"{sum(tot.values())} of {len(cases)} cases run: " +
                ", ".join(f"{k} {v}" for k, v in tot.most_common()) + "\n\n")
        for g in sorted(groups):
            c = groups[g]
            f.write(f"{g:60s} {sum(c.values()):6d}  " +
                    ", ".join(f"{k} {v}" for k, v in c.most_common()) + "\n")
    return tot

def prepare(out):
    rc, o = ssh(f"{DEQP}/gl-ready.sh", 150)
    return o.strip().endswith("ok")

def cycle(out):
    for i in range(3):
        log("power-cycling console", out)
        if subprocess.run([CYCLE], capture_output=True, timeout=1500).returncode == 0:
            time.sleep(5)
            return True
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("caselist"); ap.add_argument("out")
    ap.add_argument("--batch", type=int, default=100)
    ap.add_argument("--filter"); ap.add_argument("--limit", type=int)
    ap.add_argument("--per-case", type=float, default=4.0, help="seconds of timeout per case")
    ap.add_argument("--skip", help="file of regexes: matching cases are recorded as Skip (known GPU hang classes)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    cases = [l.strip() for l in open(a.caselist) if l.strip() and not l.startswith("#")]
    if a.filter:
        cases = [c for c in cases if re.search(a.filter, c)]
    if a.limit:
        cases = cases[:a.limit]
    if a.skip:
        pats = [re.compile(l.strip()) for l in open(a.skip) if l.strip() and not l.startswith("#")]
        done = load_done(a.out)
        for c in cases:
            if c not in done and any(p.search(c) for p in pats):
                record(a.out, c, "Skip", "known hang class (" + a.skip + ")")
    rdir = f"{NFS}{DEQP}/runs/{os.path.basename(os.path.abspath(a.out))}"
    os.makedirs(rdir, exist_ok=True)
    os.chmod(rdir, 0o777)
    n = 0
    empty = 0
    while True:
        done = load_done(a.out)
        todo = [c for c in cases if c not in done]
        if not todo:
            break
        st = gpu_state()
        if st is None or st[0] or st[1]:
            log(f"console not usable ({st}); cycling", a.out)
            if not cycle(a.out):
                log("console did not come back; stopping", a.out); break
            continue
        if not prepare(a.out):
            log("could not enable msm_compat; cycling", a.out)
            if not cycle(a.out):
                break
            continue
        batch = todo[:a.batch]
        n += 1
        bl, qpa = f"{rdir}/b{n}.txt", f"{rdir}/b{n}.qpa"
        with open(bl, "w") as f:
            f.write("\n".join(batch) + "\n")
        if os.path.exists(qpa):
            os.unlink(qpa)
        rb, qb = bl[len(NFS):], qpa[len(NFS):]
        tmo = int(30 + a.per_case * len(batch))
        t0 = time.time()
        rc, o = ssh(f"{DEQP}/run-watched.sh {rb} {qb} {tmo}", tmo + 60)
        dt = time.time() - t0
        res, running = parse_qpa(qpa)
        ev = open(qpa + ".events").read().split("\n") if os.path.exists(qpa + ".events") else []
        miss = next((l.split(None, 1)[1] if len(l.split()) > 1 else "" for l in ev if l.startswith("miss")), None)
        frozen = rc is None
        log(f"batch {n}: {len(batch)} cases from {batch[0]}, {len(res)} results in {dt:.0f}s, "
            f"running={running}, miss={miss}, frozen={frozen}, events={[l for l in ev if l]}", a.out)
        if miss is None and not frozen:
            for c, s_, d in res:
                record(a.out, c, s_, d)
            if running:
                record(a.out, running, "Crash", "process died: " + " ".join(l for l in ev if l))
            elif not res:
                record(a.out, batch[0], "Crash", "no results: " + " ".join(l for l in ev if l))
                empty += 1
                if empty >= 5:
                    log("5 batches in a row produced no results; the setup is broken, stopping", a.out)
                    break
            if res:
                empty = 0
        else:
            # the GPU wedged (missed fence) or the console froze while `culprit` was running
            culprit = miss or running or next((c for c in batch if c not in {r[0] for r in res}), batch[0])
            for c, s_, d in res:
                if c == culprit:
                    break
                record(a.out, c, s_, d)
            record(a.out, culprit, "Hang", "missed fence" if miss is not None else "console froze")
            log(f"hang blamed on {culprit}", a.out)
            if not cycle(a.out):
                log("console did not come back; stopping", a.out); break
        summary(a.out, cases)
    tot = summary(a.out, cases)
    log("finished: " + ", ".join(f"{k} {v}" for k, v in tot.most_common()), a.out)

if __name__ == "__main__":
    main()
