# E6B SUB post-expansion ISA gap audit

This is a report-only audit of final linked ELF disassembly. It does not change the strict `ARCH=minirv-npc` image check or execute images with ISA gaps.

## Current supported instruction whitelist

`ADD`, `ADDI`, `LUI`, `LW`, `LBU`, `SW`, `SB`, `JALR`, `EBREAK`, `AUIPC`, `BEQ`, `BNE`, `SLTIU`, `SUB`

## Tests newly unlocked by E6B-2A

`add`, `fib`

## Tests attempted

| Test | Present | Build | Compile | Assemble | Link | ISA audit | Execution |
|---|---:|---|---|---|---|---|---|
| add | True | linked | pass | pass | pass | pass | not_run_audit_only |
| bit | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| fact | True | fail | pass | pass | fail | not_run | not_run |
| fib | True | linked | pass | pass | pass | pass | not_run_audit_only |
| if-else | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| load-store | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| max | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| min3 | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| shift | True | linked | pass | pass | pass | unsupported | skipped_unsupported |
| string | True | fail | pass | pass | fail | not_run | not_run |
| sum | True | linked | pass | pass | pass | pass | not_run_audit_only |
| switch | True | linked | pass | pass | pass | unsupported | skipped_unsupported |

## Per-test instruction counts

| Test | Supported | Unsupported |
|---|---|---|
| add | `add` 2, `addi` 18, `auipc` 8, `beq` 1, `bne` 2, `ebreak` 1, `jalr` 8, `lui` 2, `lw` 12, `sltiu` 1, `sub` 1, `sw` 11 | — |
| bit | `add` 2, `addi` 53, `auipc` 26, `beq` 1, `bne` 1, `ebreak` 1, `jalr` 32, `lbu` 4, `lui` 2, `lw` 4, `sb` 4, `sltiu` 1, `sw` 3 | `and` 2, `andi` 7, `or` 1, `sll` 2, `sltu` 1, `srai` 2, `xori` 5 |
| fact | — | — |
| fib | `add` 1, `addi` 12, `auipc` 6, `beq` 1, `bne` 1, `ebreak` 1, `jalr` 6, `lui` 2, `lw` 6, `sltiu` 1, `sub` 1, `sw` 5 | — |
| if-else | `addi` 27, `auipc` 6, `beq` 1, `bne` 1, `ebreak` 1, `jalr` 8, `lui` 2, `lw` 5, `sltiu` 1, `sub` 3, `sw` 4 | `andi` 2, `blt` 6, `slt` 2 |
| load-store | `add` 1, `addi` 24, `auipc` 14, `beq` 1, `bne` 3, `ebreak` 1, `jalr` 10, `lbu` 12, `lui` 2, `lw` 13, `sltiu` 6, `sub` 6, `sw` 8 | `lh` 1, `lhu` 1, `or` 9, `sh` 1, `sll` 1, `slli` 10, `srli` 1, `xori` 1 |
| max | `addi` 20, `auipc` 8, `beq` 1, `bne` 2, `ebreak` 1, `jalr` 8, `lui` 2, `lw` 12, `sltiu` 1, `sub` 1, `sw` 11 | `bge` 2 |
| min3 | `add` 1, `addi` 28, `auipc` 11, `beq` 1, `bne` 3, `ebreak` 1, `jalr` 9, `lui` 2, `lw` 17, `sltiu` 1, `sub` 1, `sw` 14 | `bge` 4, `slli` 1 |
| shift | `addi` 25, `auipc` 10, `beq` 1, `bne` 3, `ebreak` 1, `jalr` 7, `lui` 2, `lw` 13, `sltiu` 3, `sub` 3, `sw` 8 | `sra` 1, `srl` 1, `srli` 1 |
| string | — | — |
| sum | `add` 2, `addi` 10, `auipc` 2, `beq` 1, `bne` 1, `ebreak` 1, `jalr` 5, `lui` 3, `lw` 3, `sltiu` 1, `sw` 4 | — |
| switch | `add` 2, `addi` 19, `auipc` 6, `beq` 3, `ebreak` 1, `jalr` 8, `lui` 2, `lw` 6, `sltiu` 1, `sub` 1, `sw` 4 | `bltu` 1, `jal` 1, `slli` 2 |

## Aggregate unsupported-instruction frequency

| Mnemonic | Count | Tests requiring it | Category |
|---|---:|---|---|
| `slli` | 13 | load-store, min3, switch | shift |
| `or` | 10 | bit, load-store | logical |
| `andi` | 9 | bit, if-else | logical |
| `bge` | 6 | max, min3 | branch |
| `blt` | 6 | if-else | branch |
| `xori` | 6 | bit, load-store | logical |
| `sll` | 3 | bit, load-store | shift |
| `and` | 2 | bit | logical |
| `slt` | 2 | if-else | comparison |
| `srai` | 2 | bit | shift |
| `srli` | 2 | load-store, shift | shift |
| `bltu` | 1 | switch | branch |
| `jal` | 1 | switch | jump |
| `lh` | 1 | load-store | load/store |
| `lhu` | 1 | load-store | load/store |
| `sh` | 1 | load-store | load/store |
| `sltu` | 1 | bit | comparison |
| `sra` | 1 | shift | shift |
| `srl` | 1 | shift | shift |

## Instruction dependency categories

| Category | Unsupported instructions |
|---|---|
| arithmetic | — |
| logical | `or`, `andi`, `xori`, `and` |
| shift | `slli`, `sll`, `srai`, `srli`, `sra`, `srl` |
| comparison | `slt`, `sltu` |
| branch | `bge`, `blt`, `bltu` |
| jump | `jal` |
| load/store | `lh`, `lhu`, `sh` |
| multiply/divide | — |
| other | — |

## Proposed smallest first implementation batch

`BGE`

This is the smallest observed unsupported-instruction set for any linked test (1 instruction(s)); it would unlock max. Frequency and test coverage break equal-size ties.

The proposal is derived only from the generated final-linked disassembly recorded in this audit; it is not implemented by this report.

## Exact build/audit errors

### fact: link

```text
# Building fact-image-dep [minirv-npc]
+ CC tests/fact.c
# Building am-archive [minirv-npc]
# Building klib-archive [minirv-npc]
# Creating image [minirv-npc]
+ LD -> build/fact-minirv-npc.elf
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/am-kernels/tests/cpu-tests/build/minirv-npc/tests/fact.o: in function `.L9':
fact.c:(.text.fact+0x28): undefined reference to `__mulsi3'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/am-kernels/tests/cpu-tests/build/minirv-npc/tests/fact.o: in function `.L19':
fact.c:(.text.startup.main+0x80): undefined reference to `__mulsi3'
collect2: error: ld returned 1 exit status
make: *** [/home/chzione/projects/abstract-machine/Makefile:150: /home/chzione/projects/am-kernels/tests/cpu-tests/build/fact-minirv-npc.elf] Error 1
```

### string: link

```text
# Building string-image-dep [minirv-npc]
+ CC tests/string.c
# Building am-archive [minirv-npc]
# Building klib-archive [minirv-npc]
# Creating image [minirv-npc]
+ LD -> build/string-minirv-npc.elf
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o): in function `.L2':
string.c:(.text.strlen+0x1c): undefined reference to `putch'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o): in function `.L3':
string.c:(.text.strlen+0x3c): undefined reference to `putch'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o): in function `.L4':
string.c:(.text.strlen+0x5c): undefined reference to `putch'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o): in function `.L11':
string.c:(.text.strcpy+0x1c): undefined reference to `putch'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o): in function `.L12':
string.c:(.text.strcpy+0x3c): undefined reference to `putch'
/usr/lib/riscv64-unknown-elf/bin/ld: /home/chzione/projects/abstract-machine/klib/build/klib-minirv-npc.a(string.o):string.c:(.text.strcpy+0x5c): more undefined references to `putch' follow
collect2: error: ld returned 1 exit status
make: *** [/home/chzione/projects/abstract-machine/Makefile:150: /home/chzione/projects/am-kernels/tests/cpu-tests/build/string-minirv-npc.elf] Error 1
```
