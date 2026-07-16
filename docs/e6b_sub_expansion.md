# E6B-2B SUB expansion

E6B-2B adds exactly the RISC-V `SUB` instruction to miniRV and the strict
Abstract Machine final-linked whitelist. It unlocks the `add` and `fib`
cpu-tests identified by the post-E6B-2A audit.

## Encoding and behavior

`SUB` uses opcode `0110011` (`0x33`), funct3 `000`, and funct7 `0100000`
(`0x20`). It computes `rd = rs1 - rs2` modulo 2^32. Integer underflow and
signed overflow do not trap; for example, `3 - 7` produces `0xfffffffc` and
`0x80000000 - 1` produces `0x7fffffff`.

The shared opcode/funct3 decoder distinguishes:

| Instruction | funct7 | Operation |
|---|---|---|
| ADD | `0000000` | `rs1 + rs2` |
| SUB | `0100000` | `rs1 - rs2` |

Every other funct7 with opcode `0x33` and funct3 `0` remains illegal. SUB is a
normal committing register write, suppresses writes to x0, and rejects rd,
rs1, or rs2 fields selecting RV32E registers x16–x31.

## Validation

- Emulator: 26/26 tests.
- RTL lint: pass.
- Standalone RTL: 27/27 tests.
- DiffTest: 36/36 tests.
- `dummy`: `HIT GOOD TRAP`, exit 0.
- `wrong`: `HIT BAD TRAP`, nonzero exit.
- `sum`: `HIT GOOD TRAP`, exit 0.
- `add`: final-linked audit pass; `HIT GOOD TRAP`, exit 0.
- `fib`: final-linked audit pass; `HIT GOOD TRAP`, exit 0.
- `max`: strict checker rejects its unsupported `BGE` instructions.
- Waveform: `build/minirv_am.fst`, generated from `add` and nonempty.

The AM runner's evaluation order was stabilized so instruction decode settles
before servicing a data-memory request after a rising edge. This prevents a
transient stale load request when an instruction such as `lw a5, 0(a5)` changes
its own base register. The guest memory map, address checks, architectural
memory behavior, reset PC, trap convention, and runner interface are unchanged.

## Final instruction inventories

The final linked no-alias `add` ELF contains 67 instructions:

| Mnemonic | Count |
|---|---:|
| add | 2 |
| addi | 18 |
| auipc | 8 |
| beq | 1 |
| bne | 2 |
| ebreak | 1 |
| jalr | 8 |
| lui | 2 |
| lw | 12 |
| sltiu | 1 |
| sub | 1 |
| sw | 11 |

The final linked no-alias `fib` ELF contains 43 instructions:

| Mnemonic | Count |
|---|---:|
| add | 1 |
| addi | 12 |
| auipc | 6 |
| beq | 1 |
| bne | 1 |
| ebreak | 1 |
| jalr | 6 |
| lui | 2 |
| lw | 6 |
| sltiu | 1 |
| sub | 1 |
| sw | 5 |

## Remaining ISA gaps

The new audit reports: `SLLI` 13, `OR` 10, `ANDI` 9, `BGE` 6, `BLT` 6,
`XORI` 6, `SLL` 3, `AND` 2, `SLT` 2, `SRAI` 2, `SRLI` 2, and one each of
`BLTU`, `JAL`, `LH`, `LHU`, `SH`, `SLTU`, `SRA`, and `SRL`. The next smallest
audit-derived batch is `BGE`, which would unlock `max`. None of these
instructions is implemented or whitelisted in E6B-2B.

## Dependency revisions

- abstract-machine: `9a44d8587c21799e4c863811e78ffce8058f0a28` on `feature/minirv-npc-e6b-sub`.
- am-kernels revision: `1600e123e48e0c672a0e6e2aa24ea9df6ee191a8`.
