# Colloquy OS

A conversation-native hobby operating system for x86_64, written from scratch in C and assembly.

Colloquy has no files, pipes or sockets. Its single kernel object is the **transcript**: an append-only, authored, SHA-256 hash-chained log of *turns*. Tasks talk by speaking and listening, and they can reach only the transcripts they hold a **seat** in. CPU time is a per-second **budget**, not a priority. The kernel records its own decisions as turns in `kernel.ledger`. The long-term goal is an OS where AI agents, including a Claude client, act through permissioned, tamper-evident conversations that a human can audit.

See [docs/DESIGN.md](docs/DESIGN.md) for the model and the roadmap.

## Status: milestone 1

Milestone 1 boots in QEMU via GRUB (Multiboot2) and enters 64-bit long mode. It provides a serial console shell, a 100 Hz preemptive budget scheduler, transcripts, seats and hash chains, the kernel ledger, and exception reporting. Four demo tasks run: `shell`, `clock`, `scribe` (relays clock turns into `notes`) and `hog` (a CPU burner capped by its budget).

In milestone 1 everything still runs in ring 0. Seats are enforced by the kernel API but not yet by hardware isolation; that is milestone 2.

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
colloquy> ps                    the hog is held to 5% CPU by its budget
colloquy> read notes            scribe's relay of what it heard from clock
colloquy> say clock hi          refused: the shell only has a listen seat
colloquy> read kernel.ledger    ...and the refusal is on the record
colloquy> tamper clock 0        corrupt a stored turn behind the kernel's back
colloquy> verify clock          BROKEN at turn #0
```

## Layout

| Path | What |
| --- | --- |
| `boot/boot.asm` | Multiboot2 header, page tables, switch to long mode |
| `boot/isr.asm` | 256 interrupt stubs; the frame swap that does context switching |
| `kernel/arch.c` | IDT, PIC, PIT timer, exception reports |
| `kernel/sched.c` | Budget scheduler, tasks, sleep and listen blocking |
| `kernel/transcript.c` | Transcripts, seats, hash chain, kernel ledger |
| `kernel/sha256.c` | FIPS 180-4 SHA-256 |
| `kernel/shell.c` | The serial shell |
| `kernel/tasks.c` | Demo tasks |
| `tests/` | Host SHA-256 test and QEMU acceptance test driver |
