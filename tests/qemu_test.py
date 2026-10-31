#!/usr/bin/env python3
"""Milestone-1 acceptance tests: boot Colloquy in QEMU headless, drive the
serial shell, and check the output. Usage: qemu_test.py build/colloquy.iso"""
import os, re, select, subprocess, sys, time

ISO = sys.argv[1] if len(sys.argv) > 1 else "build/colloquy.iso"
PROMPT = "colloquy> "

class VM:
    def __init__(self):
        self.p = subprocess.Popen(
            ["qemu-system-x86_64", "-cdrom", ISO, "-m", "128M", "-display", "none",
             "-serial", "stdio", "-no-reboot", "-monitor", "none"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.buf = ""
        self.t0 = time.time()

    def read_until(self, pat, timeout):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pat, self.buf)
            if m: return m
            r, _, _ = select.select([self.p.stdout], [], [], 0.1)
            if r:
                data = os.read(self.p.stdout.fileno(), 4096)
                if not data: break
                self.buf += data.decode("utf-8", "replace").replace("\r", "")
        return None

    def cmd(self, line, timeout=5):
        self.buf = ""
        for ch in line + "\r":          # type it, as a person would
            self.p.stdin.write(ch.encode()); self.p.stdin.flush(); time.sleep(0.005)
        self.read_until(re.escape(PROMPT), timeout)
        out = self.buf.split("\n", 1)[1] if "\n" in self.buf else ""
        return out.replace(PROMPT, "")

    def close(self):
        self.p.kill(); self.p.wait()

results = []
def check(name, ok, detail=""):
    results.append(ok)
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail and not ok else ""))

vm = VM()
try:
    m = vm.read_until(re.escape(PROMPT), 10)
    boot_s = time.time() - vm.t0
    check(f"boot smoke test: prompt in {boot_s:.1f}s (limit 5s)", m is not None and boot_s < 5, vm.buf[-300:])

    u1 = vm.cmd("uptime"); time.sleep(1.5); u2 = vm.cmd("uptime")
    n1 = int(re.search(r"(\d+) ticks", u1).group(1)); n2 = int(re.search(r"(\d+) ticks", u2).group(1))
    check("timer advances (~100 Hz)", 100 <= n2 - n1 <= 300, f"{n1}->{n2}")
    ps = vm.cmd("ps")
    check("scheduler: all demo tasks present", all(t in ps for t in ["idle", "shell", "clock", "scribe", "hog"]), ps)

    # Budget enforcement: hog has 5 ticks/s; sample its lifetime CPU over 10 s.
    def hog_line(): return vm.cmd("ps").split("hog", 1)[1].split("\n")[0]
    vm.cmd("budget 4 5"); time.sleep(1.2)
    start_ticks = int(re.search(r"(\d+) ticks", vm.cmd("uptime")).group(1))
    samples = []
    for _ in range(10):
        time.sleep(1.0)
        samples.append(int(re.search(r"(\d+)%", hog_line()).group(1)))
    avg = sum(samples) / len(samples)
    check(f"budget: hog capped at 5% (measured avg {avg:.1f}%)", 4 <= avg <= 6, str(samples))
    vm.cmd("budget 4 20"); time.sleep(2.2)
    pct = int(re.search(r"(\d+)%", hog_line()).group(1))
    check(f"budget: raising hog to 20/s lets it use ~20% (got {pct}%)", 18 <= pct <= 22)
    vm.cmd("budget 4 5")

    # Transcript IPC: clock speaks every 5 s; scribe relays into notes.
    time.sleep(5.5)
    clock = vm.cmd("read clock 50"); notes = vm.cmd("read notes 50")
    cseq = re.findall(r"#\s*(\d+)\s+t=\s*\d+\s+clock\s+\w+ \| (tick \d+)", clock)
    nseq = re.findall(r"#\s*\d+\s+t=\s*\d+\s+scribe\s+\w+ \| heard clock #(\d+) from task 2: \"(tick \d+)", notes)
    check(f"IPC: scribe heard every clock turn in order ({len(nseq)}/{len(cseq)})",
          len(cseq) >= 3 and nseq[:len(cseq)] == cseq[:len(nseq)] and len(nseq) >= len(cseq) - 1, clock + notes)

    # Seats: the shell may speak into chat, but only listen to clock.
    r = vm.cmd("say chat hello from the shell")
    check("seat: speak allowed with speak seat", "spoke turn #0 into chat" in r, r)
    r = vm.cmd("say clock let me in")
    check("seat: speak refused without speak seat", "denied" in r, r)
    led = vm.cmd("read kernel.ledger 5")
    check("seat: refusal logged in kernel.ledger", "DENIED speak by task 1" in led, led)
    r = vm.cmd("read hogdiary")
    check("seat: unknown/uninvited transcript unreachable", "denied" in r, r)

    # Tamper detection.
    r = vm.cmd("verify clock")
    check("verify: clean transcript passes", "OK" in r and "chain intact" in r, r)
    vm.cmd("tamper clock 1")
    r = vm.cmd("verify clock")
    check("verify: tampered transcript fails at exact turn #1", "BROKEN at turn #1" in r, r)
    r = vm.cmd("verify notes")
    check("verify: other transcripts unaffected", "OK" in r, r)

    # Fault handling (last: it halts the machine).
    vm.buf = ""
    for ch in "fault\r": vm.p.stdin.write(ch.encode()); vm.p.stdin.flush(); time.sleep(0.005)
    m = vm.read_until(r"system halted", 5)
    check("fault: exception reported, not a silent reboot", m is not None and "invalid opcode" in vm.buf, vm.buf)
finally:
    vm.close()

print(f"\n{sum(results)}/{len(results)} checks passed")
sys.exit(0 if all(results) else 1)
