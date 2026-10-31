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

Tasks need only four operations, which become the entire system call surface in milestone 2:

- `open(name)` creates a transcript and gives the caller a full seat (listen, speak, invite).
- `invite(seat, task, rights)` hands another task a seat with a subset of the caller's rights. This is the only way access spreads.
- `speak(seat, text)` appends a turn and wakes every listener.
- `listen(seat, cursor)` returns the next turn after `cursor`, blocking until it exists.

Turn hash: `SHA-256(prev_hash || seq || author || tick || kind || len || text)`.

## Scheduler

Timer at 100 Hz. Each task has `budget` ticks per epoch (100 ticks). Each tick charges the running task. Ready tasks with budget left run round-robin. The idle task runs only when no other task can. Budgets refill at each epoch boundary. `ps` shows the last epoch's use, so a 5/s task reads 5%.

## Known limits of milestone 1

- All tasks run in ring 0 and share one address space, so seats are enforced by the API, not by hardware.
- Transcripts hold at most 256 turns of up to 96 bytes and live only in RAM.
- Interaction is over the serial console only; there is no keyboard driver.
- `tamper` is a deliberate debug backdoor, there to demonstrate detection.

## Roadmap

1. **M1, done:** boot, interrupts, budget scheduler, transcripts, seats, hash chains, ledger, shell.
2. **M2:** ring-3 tasks with their own page tables; `int 0x80` system calls limited to the four verbs.
3. **M3:** virtio-blk driver as a task; transcripts persist and are verified at boot.
4. **M4:** virtio-net, TCP/IP and TLS (likely ported lwIP and BearSSL).
5. **M5:** a Claude API client whose requests, replies and tool actions are transcript turns.
