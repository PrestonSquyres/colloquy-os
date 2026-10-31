#!/usr/bin/env python3
"""Milestone 1 and 2 acceptance tests: boot Colloquy in QEMU headless, drive the
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
    def hog_line(): return vm.cmd("ps").split(" hog ", 1)[1].split("\n")[0]
    hog_id = int(re.search(r"^\s*(\d+)\s+hog\b", ps, re.M).group(1))
    vm.cmd(f"budget {hog_id} 5"); time.sleep(1.2)
    samples = []
    for _ in range(10):
        time.sleep(1.0)
        samples.append(int(re.search(r"(\d+)%", hog_line()).group(1)))
    avg = sum(samples) / len(samples)
    check(f"budget: hog capped at 5% (measured avg {avg:.1f}%)", 4 <= avg <= 6, str(samples))
    vm.cmd(f"budget {hog_id} 20"); time.sleep(2.2)
    pct = int(re.search(r"(\d+)%", hog_line()).group(1))
    check(f"budget: raising hog to 20/s lets it use ~20% (got {pct}%)", 18 <= pct <= 22)
    vm.cmd(f"budget {hog_id} 5")

    # Transcript IPC: clock speaks every 5 s; scribe relays into notes.
    time.sleep(5.5)
    clock = vm.cmd("read clock 50"); notes = vm.cmd("read notes 50")
    cseq = re.findall(r"#\s*(\d+)\s+t=\s*\d+\s+task \d+\s+\w+ \| (tick \d+)", clock)
    nseq = re.findall(r"#\s*\d+\s+t=\s*\d+\s+task \d+\s+\w+ \| heard clock #(\d+) from task \d+: \"(tick \d+)", notes)
    check(f"IPC: scribe heard every clock turn in order ({len(nseq)}/{len(cseq)})",
          len(cseq) >= 3 and nseq[:len(cseq)] == cseq[:len(nseq)] and len(nseq) >= len(cseq) - 1, clock + notes)

    # Seats: the shell may speak into chat, but only listen to clock.
    r = vm.cmd("say chat hello from the shell")
    check("seat: speak allowed with speak seat", "spoke turn #0 into chat" in r, r)
    r = vm.cmd("say clock let me in")
    check("seat: speak refused without speak seat", "denied" in r, r)
    led = vm.cmd("read kernel.ledger 5")
    check("seat: refusal logged in kernel.ledger", re.search(r"DENIED speak by task \d+ 'shell'", led) is not None, led)
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

    # ---------------- Milestone 2: user mode and isolation ----------------
    ps = vm.cmd("ps")
    rings = dict((n, int(r)) for n, r in re.findall(r"^\s*\d+\s+(\S+)\s+(\d)\s", ps, re.M))
    check("M2 ring 3: shell, clock, scribe, hog are user programs",
          all(rings.get(n) == 3 for n in ["shell", "clock", "scribe", "hog"]), ps)

    def new_probe():
        r = vm.cmd("spawn probe"); pid = int(re.search(r"as task (\d+)", r).group(1))
        vm.cmd(f"invite {pid} chat ls"); time.sleep(0.4)
        return pid
    def probe_say(pid, c, wait=0.4):
        vm.cmd(f"say chat @{pid} {c}"); time.sleep(wait)
        return vm.cmd("read chat 20")
    def task_line(pid):
        ps = vm.cmd("ps"); m = re.search(rf"^\s*{pid}\s+probe.*(?:\n\s+died: .*)?", ps, re.M)
        return m.group(0) if m else ps

    p1 = r = vm.cmd("spawn probe"); p1 = int(re.search(r"as task (\d+)", r).group(1)); time.sleep(0.3)
    so = vm.cmd(f"seatsof {p1}")
    check("M2 inbox-only birth: new program holds exactly its inbox", f"holds 1 seat" in so and "inbox." in so, so)
    led = vm.cmd("read kernel.ledger 8")
    check("M2 inbox-only birth: its speak attempts are all refused and logged",
          len(re.findall(rf"DENIED speak by task {p1} 'probe'", led)) == 4, led)
    vm.cmd(f"invite {p1} chat ls"); time.sleep(0.4)
    chat = vm.cmd("read chat 20")
    check("M2 invitations: invited program reports in on chat", f"probe {p1}: ready; born with 1 seat(s); 4 of 4" in chat, chat)
    chat = probe_say(p1, "ring")
    check("M2 ring 3: probe reports CPL 3", f"probe {p1}: running at ring 3" in chat, chat)
    chat = probe_say(p1, "badsys")
    check("M2 syscall surface: unknown call rejected, program lives", "returned -8 (no such system call)" in chat, chat)
    led = vm.cmd("read kernel.ledger 3")
    check("M2 syscall surface: rejection logged", "REFUSED unknown system call 99" in led, led)
    chat = probe_say(p1, "badptr")
    check("M2 pointer check: kernel pointer as turn text refused", "returned -7 (bad pointer)" in chat, chat)
    r = vm.cmd("verify chat")
    check("M2 invitations: chat with probe turns verifies", "chain intact" in r, r)

    chat = probe_say(p1, "kwrite")
    tl = task_line(p1)
    check("M2 kernel memory protected: write kills the program", "dead" in tl and "page fault" in tl and "0x100000" in tl
          and "SUCCEEDED" not in chat, tl + chat)
    r = vm.cmd("uptime")
    check("M2 crash containment: shell still answers after a kill", "ticks" in r, r)

    m = re.search(r"physical (0x[0-9a-f]+)", vm.cmd(f"where {hog_id}")); hog_phys = m.group(1)
    p2 = new_probe()
    chat = probe_say(p2, f"peek {hog_phys}")
    tl = task_line(p2)
    check("M2 memory isolation: reading another program's memory kills the reader",
          "dead" in tl and "page fault" in tl and "reachable" not in chat, tl + chat)
    time.sleep(1.2)
    check("M2 memory isolation: the other program keeps running", re.search(r"(\d+)%", hog_line()).group(1) != "0", hog_line())

    p3 = new_probe()
    probe_say(p3, "ud2"); tl = task_line(p3)
    check("M2 crash containment: invalid instruction kills only that program", "dead" in tl and "invalid opcode" in tl, tl)
    p4 = new_probe()
    chat = probe_say(p4, "io"); tl = task_line(p4)
    check("M2 no hardware access: I/O port write from ring 3 kills the program",
          "dead" in tl and "general protection" in tl and "SUCCEEDED" not in chat, tl)
    ps = vm.cmd("ps")
    check("M2 crash containment: all other tasks still alive",
          all(re.search(rf"^\s*\d+\s+{n}\s+\d\s+(ready|sleep|listen)", ps, re.M) for n in ["shell", "clock", "scribe", "hog", "desk"]), ps)

    # Console as transcripts: what we typed is on the record.
    vm.cmd("say chat recorded keystrokes")
    cin = vm.cmd("read console.in 3")
    check("M2 console: typed lines are turns on console.in, authored by the driver",
          re.search(r"task \d+\s+\w+ \| say chat recorded keystrokes", cin) is not None, cin)
    r = vm.cmd("verify console.in")
    check("M2 console: console.in chain verifies", "chain intact" in r, r)

    # Memory holds the newest 256 turns; the chain stays verifiable from its anchor.
    for _ in range(25): vm.cmd("ps")
    ls = vm.cmd("ls"); m = re.search(r"console.out\s+(\d+)\s+(\d+)", ls)
    r = vm.cmd("verify console.out")
    check("M2 rollover: console.out dropped old turns and still verifies",
          m and int(m.group(1)) > 256 and int(m.group(2)) == 256 and "chain intact" in r, ls + r)

    # Kernel fault handling (last: it halts the machine).
    vm.buf = ""
    for ch in "kfault\r": vm.p.stdin.write(ch.encode()); vm.p.stdin.flush(); time.sleep(0.005)
    m = vm.read_until(r"system halted", 5)
    check("fault: kernel exception reported, not a silent reboot", m is not None and "invalid opcode" in vm.buf, vm.buf)
finally:
    vm.close()

print(f"\n{sum(results)}/{len(results)} checks passed")
sys.exit(0 if all(results) else 1)
