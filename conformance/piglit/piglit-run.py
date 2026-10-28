#!/usr/bin/env python3
"""Run piglit tests on the Xbox 360 over SSH, surviving GPU hangs (like deqp-run.py).

Tests come from a piglit test list (quick_gl.xml.gz etc. on the NFS root). Each batch
runs on the console through run-batch.sh, which prints "name<TAB>result" per test. If
a batch leaves the GPU hung (fence timeout/stall) or the console stops answering, the
test that was running is recorded as hang and the console is power-cycled.
State is in <out>/results.tsv, so a run resumes where it stopped.

usage: piglit-run.py <list.xml.gz> <outdir> [--filter REGEX] [--names FILE] [--batch N] [--skip FILE]
"""
import argparse, gzip, os, re, subprocess, sys, time, ast, xml.etree.ElementTree as ET

NFS = "/srv/nfs/xenon"
PIGLIT = "/usr/lib/powerpc64-linux-gnu/piglit"
SSH = ["ssh", "-o", "ConnectTimeout=5", "-o", "BatchMode=yes", "-o", "ServerAliveInterval=5",
       "-o", "ServerAliveCountMax=3", "-i", "/home/arelyx/.ssh/lake", "root@10.0.0.206"]
CYCLE = "/home/arelyx/peer_pressure/netboot/irblaster/xbox-cycle"

def log(out, msg):
    line = time.strftime("%F %T ") + msg
    print(line, flush=True)
    open(os.path.join(out, "run.log"), "a").write(line + "\n")

def ssh(cmd, timeout):
    try:
        r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=timeout)
        return r.returncode, r.stdout
    except subprocess.TimeoutExpired:
        return -1, ""

def load_tests(path, flt):
    tests = []
    for t in ET.fromstring(gzip.open(path).read()).iter("Test"):
        name = t.get("name")
        if flt and not re.search(flt, name):
            continue
        opts = {o.get("name"): o.get("value") for o in t.iter("option")}
        cmd = ast.literal_eval(opts.get("command", "[]"))
        if not cmd:
            continue
        # as piglit's PiglitGLTest: -fbo only for concurrent tests; the
        # others (fbo-blit-stretch reads framebuffer 0) run in the window
        flags = ["-auto", "-fbo"] if opts.get("run_concurrent") != "False" else ["-auto"]
        if t.get("type") in ("gl", "shader"):
            cmd = cmd + flags
        tests.append((name, cmd))
    return tests

def done(out):
    p = os.path.join(out, "results.tsv")
    return {l.split("\t")[0]: l.rstrip("\n").split("\t")[1] for l in open(p)} if os.path.exists(p) else {}

def record(out, name, res):
    open(os.path.join(out, "results.tsv"), "a").write(f"{name}\t{res}\n")

def gpu_ok():
    rc, o = ssh("cat /sys/module/xegpu/parameters/fence_timeouts /sys/module/xegpu/parameters/fence_stalls /sys/module/xegpu/parameters/gpu_hung", 20)
    v = o.split()
    return rc == 0 and v[:3] == ["0", "0", "N"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("list"); ap.add_argument("out")
    ap.add_argument("--filter"); ap.add_argument("--batch", type=int, default=40)
    ap.add_argument("--per-test", type=int, default=30)
    ap.add_argument("--skip", help="file of regexes recorded as skip (known hangs)")
    ap.add_argument("--names", help="file of test names to run (one per line)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tests = load_tests(a.list, a.filter)
    if a.names:
        want = set(l.rstrip("\n") for l in open(a.names) if l.strip())
        tests = [t for t in tests if t[0] in want]
    skips = [re.compile(l.strip()) for l in open(a.skip) if l.strip() and not l.startswith("#")] if a.skip else []
    rdir = f"{NFS}/opt/piglit-runs/{os.path.basename(os.path.abspath(a.out))}"
    os.makedirs(rdir, exist_ok=True); os.chmod(rdir, 0o777)
    log(a.out, f"{len(tests)} tests")
    n = 0
    while True:
        d = done(a.out)
        todo = [t for t in tests if t[0] not in d]
        for t in todo:
            if any(p.search(t[0]) for p in skips):
                record(a.out, t[0], "skip-known-hang")
        todo = [t for t in todo if not any(p.search(t[0]) for p in skips)]
        if not todo:
            break
        if not gpu_ok():
            log(a.out, "console/GPU not usable; cycling")
            subprocess.run([CYCLE, "force"], capture_output=True)
            continue
        ssh("/opt/deqp/gl-ready.sh", 150)
        batch = todo[:a.batch]; n += 1
        bf = f"{rdir}/b{n}.txt"
        with open(bf, "w") as f:
            for name, cmd in batch:
                f.write(name + "\t" + " ".join("'" + c.replace("'", "'\\''") + "'" for c in cmd) + "\n")
        rf = f"{rdir}/b{n}.res"
        if os.path.exists(rf): os.unlink(rf)
        t0 = time.time()
        rc, _ = ssh(f"/opt/piglit-runs/run-batch.sh {bf[len(NFS):]} {rf[len(NFS):]} {a.per_test}", a.per_test * len(batch) + 120)
        got = {}
        if os.path.exists(rf):
            for l in open(rf):
                p = l.rstrip("\n").split("\t")
                if len(p) >= 2: got[p[0]] = p[1]
        for name, _ in batch:
            if name in got and got[name] != "running":
                record(a.out, name, got[name])
        running = [k for k, v in got.items() if v == "running"]
        hung = not gpu_ok() or "hang" in got.values()
        log(a.out, f"batch {n}: {len(batch)} tests, {len([1 for k in got if got[k] != 'running'])} results in {time.time()-t0:.0f}s, rc={rc} running={running} hung={hung}")
        if hung or rc == -1:
            culprit = running[0] if running else None
            if culprit:
                record(a.out, culprit, "hang")
            subprocess.run([CYCLE, "force"], capture_output=True)
    res = done(a.out)
    from collections import Counter
    log(a.out, "totals: " + ", ".join(f"{k} {v}" for k, v in Counter(res.values()).most_common()))

if __name__ == "__main__":
    main()
