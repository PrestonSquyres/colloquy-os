# Colloquy OS

A conversation-native hobby operating system for x86_64, written from scratch in C and assembly.

Colloquy has no files, pipes or sockets. Its single kernel object is the **transcript**: an append-only, authored, SHA-256 hash-chained log of *turns*. Tasks talk by speaking and listening, and they can reach only the transcripts they hold a **seat** in. CPU time is a per-second **budget**, not a priority. The kernel records its own decisions as turns in `kernel.ledger`. The long-term goal is an OS where AI agents, including a Claude client, act through permissioned, tamper-evident conversations that a human can audit.

See [docs/DESIGN.md](docs/DESIGN.md) for the model and the roadmap.

## Status: milestone 2

Colloquy boots in QEMU via GRUB (Multiboot2) into 64-bit long mode. It has a 100 Hz preemptive budget scheduler, transcripts with seats and hash chains, and the kernel ledger.

Milestone 2 makes the rules hardware-enforced:

- **Ring 3 programs.** `shell`, `clock`, `scribe`, `hog` and `probe` are separate ELF programs, each in its own address space. They cannot touch kernel memory, each other's memory, or I/O ports.
- **System calls.** Programs reach the kernel through `int 0x80`. The calls are the four verbs (`open`, `invite`, `speak`, `listen`), plus `exit`, `sleep`, and three that only describe the caller's own seats. Every pointer is checked against the caller's own memory.
- **Inbox-only birth.** A new program holds exactly one seat: listen on its own `inbox.<id>`. Whenever it is invited somewhere, the kernel tells it so in that inbox.
- **Fault containment.** A program that faults is killed and the cause is written to `kernel.ledger`; everything else keeps running.
- **The console is a conversation.** A driver turns typed lines into turns on `console.in` and prints `console.out`. The shell is an ordinary program holding seats on both.
- **The kernel is a participant.** The shell asks the kernel questions (`ps`, `ls`, `verify` ...) by speaking into `kernel.desk`. Holding a speak seat there is the authority to ask.

Memory holds the newest 256 turns of each transcript. Older turns are dropped but their hash stays as the chain's anchor, so what remains still verifies. Milestone 3 will move old turns to disk instead.

## Build and run

Requires `gcc`, `nasm`, `ld`, `grub-mkrescue` (packages `grub-pc-bin grub-common xorriso mtools`) and `qemu-system-x86_64`.

```sh
make          # builds build/colloquy.iso
make run      # boots it in QEMU on this terminal (quit: Ctrl-A then X)
make test     # SHA-256 test vectors + headless QEMU acceptance tests
```

## Try it

```
colloquy> about                 what makes Colloquy different
colloquy> ps                    every program runs in ring 3; the hog is held to 5% CPU
colloquy> say clock hi          refused: the shell only has a listen seat on clock
colloquy> read kernel.ledger    ...and the refusal is on the record
colloquy> spawn probe           start the misbehaving test program (say it is task 9)
colloquy> seatsof 9             born with exactly one seat: its inbox
colloquy> invite 9 chat ls      let it listen and speak in chat
colloquy> say chat @9 kwrite    it tries to write kernel memory...
colloquy> ps                    ...and is dead with a page fault; nothing else noticed
colloquy> tamper clock 0        corrupt a stored turn behind the kernel's back
colloquy> verify clock          BROKEN at turn #0
```

Probe commands (sent as `say chat @<id> <command>`): `ring`, `hello`, `badsys`, `badptr`, `kwrite`, `peek <addr>`, `ud2`, `io`. A recorded session is in [docs/demo-session.txt](docs/demo-session.txt).

## Layout

| Path | What |
| --- | --- |
| `boot/boot.asm` | Multiboot2 header, boot page tables, switch to long mode |
| `boot/isr.asm` | 256 interrupt stubs; the frame swap that does context switching |
| `kernel/arch.c` | IDT, PIC, PIT timer, exceptions (kill user programs, halt on kernel faults) |
| `kernel/gdt.c` | GDT with ring-3 segments and the TSS |
| `kernel/mm.c` | Physical frames, per-program page tables, user pointer checks |
| `kernel/elf.c` | Boot modules and the ELF loader |
| `kernel/syscall.c` | The `int 0x80` system call gate |
| `kernel/sched.c` | Budget scheduler; switches CR3 and kernel stack per task |
| `kernel/transcript.c` | Transcripts, seats, inboxes, hash chain, kernel ledger |
| `kernel/desk.c` | `kernel.desk` (the kernel answering questions) and boot wiring |
| `kernel/consoled.c` | Console driver: serial line to `console.in` / `console.out` |
| `kernel/abi.h` | System call numbers and structures shared with programs |
| `user/` | `libcolloquy` and the programs: shell, clock, scribe, hog, probe |
| `grub.cfg` | Which programs load at boot, their budgets, and which wait for `spawn` |
| `tests/` | Host SHA-256 test and QEMU acceptance tests (33 checks) |
