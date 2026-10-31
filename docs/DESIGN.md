# Colloquy design

## Core idea

Every interaction between programs is a conversation, and every conversation is on the record.

| Concept | Colloquy | Typical OS |
| --- | --- | --- |
| Kernel object | **Transcript**: append-only log of authored turns | Files, pipes, sockets, shared memory |
| Naming and access | **Seats**: per-task capabilities (listen, speak, invite) to one transcript; no global namespace | Path names plus permission bits or ACLs |
| Integrity | Each turn stores SHA-256 over the previous turn's hash and its own fields; `verify` replays the chain | Mutable data; integrity is an add-on |
| Provenance | Kernel stamps author task, sequence and tick on every turn | Usually unknown who wrote what |
| CPU sharing | **Budgets**: ticks per one-second epoch; spent budget means waiting, even on an idle CPU | Priorities or fair share |
| Kernel audit | `kernel.ledger`: the kernel's own decisions (spawns, opens, invites, denials) as turns | Separate, optional logging |

## The four verbs

Programs need only four operations, which are the heart of the system call surface:

- `open(name)` creates a transcript and gives the caller a full seat (listen, speak, invite).
- `invite(seat, task, rights)` hands another task a seat with a subset of the caller's rights. This is the only way access spreads, and the kernel tells the invitee in its inbox.
- `speak(seat, text)` appends a turn and wakes every listener.
- `listen(seat, cursor)` returns the next turn after `cursor`, blocking until it exists (or returning at once with `LISTEN_NOWAIT`).

The remaining calls are housekeeping: `exit`, `sleep`, and `seat_find`, `seat_info` and `self`, which describe only the caller's own seats and identity. There is no call that names or reaches anything the caller was not invited to. See `kernel/abi.h`.

Turn hash: `SHA-256(prev_hash || seq || author || tick || kind || len || text)`.

## Isolation (milestone 2)

- Every program has its own PML4. Slot 0 of each PDPT maps the kernel's first GiB without the user bit; the program lives at `0x40000000`-`0x80000000` with its stack just below the top.
- Ring 3 runs with IOPL 0 and no I/O bitmap, so port access faults.
- The TSS points interrupts from ring 3 at the running task's own kernel stack; the scheduler switches CR3 and that stack together.
- System call arguments are copied in and out only after every page they span is checked as present and user-accessible in the caller's tables.
- A fault in ring 3 kills that program and writes the reason to `kernel.ledger`. A fault in ring 0 halts the machine with a report.

## Birth and wiring

A program is born holding one seat: listen on `inbox.<id>`, a transcript only the kernel speaks into. Everything else must be invited. At boot, kernel tasks invite the shell to `console.in`, `console.out` and `kernel.desk`. The kernel's `init` task introduces programs to each other from a small table, for example scribe gets listen on clock. Every grant is a turn in `kernel.ledger`.

## Scheduler

Timer at 100 Hz. Each task has `budget` ticks per epoch (100 ticks). Each tick charges the running task. Ready tasks with budget left run round-robin. The idle task runs only when no other task can. Budgets refill at each epoch boundary. `ps` shows the last epoch's use, so a 5/s task reads 5%.

## Known limits

- Kernel tasks (console driver, desk, init) still run in ring 0. Moving drivers to ring 3 needs a way to grant I/O access through seats.
- Transcripts live only in RAM, holding the newest 256 turns of up to 96 bytes each; older turns are dropped and summarized by the anchor hash.
- The FPU/SSE state is not saved on task switches, so programs are compiled with general registers only.
- A dead program's memory is reclaimed when its task slot is reused, not immediately.
- `tamper` is a deliberate debug backdoor on `kernel.desk`, there to demonstrate detection.

## Roadmap

1. **M1, done:** boot, interrupts, budget scheduler, transcripts, seats, hash chains, ledger, shell.
2. **M2, done:** ring-3 programs in their own address spaces; `int 0x80` system calls built around the four verbs; inbox-only birth; console and kernel desk as transcripts.
3. **M3:** virtio-blk driver as a task; transcripts persist and are verified at boot.
4. **M4:** virtio-net, TCP/IP and TLS (likely ported lwIP and BearSSL).
5. **M5:** a Claude API client whose requests, replies and tool actions are transcript turns.
