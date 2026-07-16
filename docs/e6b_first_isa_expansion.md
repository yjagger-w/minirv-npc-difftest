# E6B-2A first miniRV ISA expansion

E6B-2A implements exactly the first audit-derived batch: `AUIPC`, `BEQ`,
`BNE`, and `SLTIU`. The original nine instructions remain supported, giving a
strict 13-mnemonic AM whitelist.

## Encodings and semantics

| Instruction | Opcode | funct3 | Result |
|---|---|---|---|
| AUIPC | `0010111` | — | `rd = pc + {instr[31:12], 12'b0}` |
| SLTIU | `0010011` | `011` | `rd = unsigned(rs1) < unsigned(sext(imm12))` |
| BEQ | `1100011` | `000` | branch when `rs1 == rs2` |
| BNE | `1100011` | `001` | branch when `rs1 != rs2` |

AUIPC uses the instruction's current PC, not the sequential PC. Its U-immediate
is `instr[31:12]` followed by 12 zero bits.

The branch immediate is constructed as
`{instr[31], instr[7], instr[30:25], instr[11:8], 1'b0}` and sign-extended
from 13 to 32 bits. A taken target is `current_pc + branch_immediate`; a
not-taken branch advances to `current_pc + 4`.

SLTIU has a deliberately mixed signed/unsigned rule: the 12-bit immediate is
first sign-extended to 32 bits, then that 32-bit bit pattern and `rs1` are
compared as unsigned values. Thus immediate `-1` compares as `0xffffffff`.

Because compressed instructions are unsupported, a taken branch target must
be four-byte aligned. A taken misaligned target raises instruction-address
misalignment without retiring the branch. A not-taken branch does not inspect
target alignment and retires sequentially. All used register fields reject
RV32E registers x16–x31; branches perform no register write.

## AM startup and layout

Startup now begins with:

```asm
lui x2, 0x88000
```

This initializes `sp` to the exclusive top of guest memory, `0x88000000`.
Reset and image loading remain at `0x80000000`, `main` remains fixed at
`0x80000020`, linker relaxation remains disabled, and the startup plus `halt`
occupy only `0x80000000` through `0x80000013`.

## Validation

- Pre-change E6A baseline: emulator 21/21, RTL 22/22, DiffTest 23/23, lint pass.
- Final emulator: 25/25.
- Final standalone RTL: 26/26; lint pass.
- Final DiffTest: 32/32.
- `dummy`: `HIT GOOD TRAP`, exit 0.
- `wrong`: `HIT BAD TRAP`, nonzero exit.
- `sum`: strict final-linked audit pass, `HIT GOOD TRAP`, exit 0.
- `sum` waveform: `build/minirv_am.fst` generated and nonempty.
- A strict normal build of `add` remains rejected because its final ELF uses
  unsupported `SUB`.

The final linked no-alias `sum` image contains 33 instructions:

| Mnemonic | Count |
|---|---:|
| add | 2 |
| addi | 10 |
| auipc | 2 |
| beq | 1 |
| bne | 1 |
| ebreak | 1 |
| jalr | 5 |
| lui | 3 |
| lw | 3 |
| sltiu | 1 |
| sw | 4 |

## Remaining ISA gaps

The E6B-1 linked-image audit still identifies `SUB`; logical operations
`AND`, `ANDI`, `OR`, `XORI`; shifts `SLL`, `SLLI`, `SRA`, `SRAI`, `SRL`,
`SRLI`; comparisons `SLT`, `SLTU`; branches `BGE`, `BLT`, `BLTU`; `JAL`;
and halfword accesses `LH`, `LHU`, `SH`. The `fact` test also cannot link
without multiplication support (`__mulsi3`). None of these gaps is implemented
or whitelisted in E6B-2A.

## Dependency revisions

- abstract-machine: `b8276807c0b52b257f219c2f8e9c9875fbb5b2b3`
- am-kernels: `1600e123e48e0c672a0e6e2aa24ea9df6ee191a8`

Both dependencies remain separate sibling repositories and are not vendored
into this repository.
