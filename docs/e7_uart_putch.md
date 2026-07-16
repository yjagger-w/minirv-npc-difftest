# E7-B simulated UART TX and AM putch

E7-B adds a simulation-only, memory-mapped UART transmit register to the E7-A
miniRV SoC. It does not change the CPU core or the supported instruction set.

## Address map and bus behavior

| Address range | Device | Behavior |
|---|---|---|
| `0x80000000`–`0x87ffffff` | RAM | Existing byte-masked read/write memory |
| `0x10000000`–`0x10000003` | GPIO input | Read-only |
| `0x10000004`–`0x10000007` | GPIO output | Byte-masked read/write |
| `0x10000008`–`0x1000000b` | UART TX | Writes transmit one byte; reads return zero |

The existing `dmem_valid`, `dmem_write`, `dmem_addr`, `dmem_wdata`,
`dmem_wmask`, and `dmem_rdata` signals remain the simple CPU data bus. The
decoder selects exactly one of RAM, GPIO, or UART. An access outside these
ranges still asserts `bus_error`.

For `SB` to any UART byte address, the bus shifts the store data and mask to
the effective byte lane and the UART emits that selected byte. `SW` at
`0x10000008` emits only its least-significant byte. UART writes do not modify
RAM or GPIO, and UART reads return zero.

## Transmit event

`minirv_uart_tx` registers `uart_tx_valid` and `uart_tx_data` on the clock edge
of an accepted write. `uart_tx_valid` defaults low on the next clock and
therefore pulses once per CPU store. Because the event is derived in clocked
RTL rather than directly from combinational `dmem_valid`, repeated Verilator
settle evaluations cannot duplicate a character.

## Abstract Machine putch

The minirv-npc AM port now includes `am/src/minirv/npc/trm.c`. Its `putch`
performs a volatile byte store to:

```c
#define UART_TX_ADDR 0x10000008U
```

Startup, the linker layout, stack address, trap convention, and strict ISA
whitelist are unchanged. The E7-B hello program calls `putch` for each byte of
`Hello miniRV\n`. Its final linked ELF passes the current strict no-alias ISA
audit with 53 instructions.

## SoC AM runner

`rtl/csrc/minirv_soc_am_runner.cpp` loads a raw image at `0x80000000`, provides
the 128 MiB simulation RAM backing, captures clocked UART events, enforces RAM
bounds and a deterministic cycle limit, and retains the existing good/bad
EBREAK convention. The Makefile targets are `soc-am-run` and `soc-am-wave`;
the latter writes `build/minirv_soc_am.fst`.

The strict-audited hello image executes through this SoC runner as:

```text
Hello miniRV
HIT GOOD TRAP
```

## Validation

- Emulator: 26/26.
- RTL lint: pass.
- Standalone RTL: 27/27.
- DiffTest: 36/36.
- SoC lint: pass.
- SoC tests: 11/11, covering GPIO, RAM, UART byte lanes, word-store single
  events, ordered multiple stores, zero reads, isolation, duplicate-event
  prevention, and unmapped `bus_error`.
- `soc-wave`: pass; `build/minirv_soc.fst` is nonempty.
- AM UART hello strict audit: pass (53 final-linked instructions).
- SoC AM hello execution and waveform: pass;
  `build/minirv_soc_am.fst` is nonempty.

The existing cpu-tests `string` program compiles and links, but its final
linked ISA audit rejects four `SRLI` instructions and one `JAL`. Execution was
not attempted, and neither instruction was implemented or whitelisted.

## Limitations

This UART is a simulation character-event device. It has no baud timing,
serial pin protocol, receive path, FIFO, interrupts, or ready/stall handshake.
The SoC still has one unstalled CPU data master and simulation-backed RAM; it
does not add arbitration, AXI, Wishbone, caches, DDR, FPGA integration, a
pipeline, or operating-system support.

## Dependency revision

- abstract-machine: `417281805c439b512038eff21f2a2e31d8fe94d5`
