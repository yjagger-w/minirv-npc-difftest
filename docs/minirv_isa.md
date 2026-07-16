# miniRV ISA emulator

## Architectural state

The emulator implements the current miniRV subset in C++17. The program counter is a
32-bit value reset to `0x00000000`. There are sixteen 32-bit integer registers,
`x0` through `x15`; `x0` is forced to zero after every retired instruction.
Any implemented instruction that encodes an operand register from `x16` through
`x31` is an illegal instruction.

Memory is a finite byte array addressed with 32-bit addresses. Multi-byte values
are little-endian. Integer address and ALU calculations wrap modulo 2^32.
Instructions are fixed-width 32-bit values, and instruction fetches must be
four-byte aligned.

## Instructions

| Instruction | Encoding constraint | Operation |
|---|---|---|
| `ADD` | opcode `0x33`, funct3 `0`, funct7 `0` | `rd = rs1 + rs2` |
| `ADDI` | opcode `0x13`, funct3 `0` | `rd = rs1 + sext(imm12)` |
| `LUI` | opcode `0x37` | `rd = instruction[31:12] << 12` |
| `AUIPC` | opcode `0x17` | `rd = pc + (instruction[31:12] << 12)` |
| `SLTIU` | opcode `0x13`, funct3 `3` | `rd = unsigned(rs1) < unsigned(sext(imm12))` |
| `BEQ` | opcode `0x63`, funct3 `0` | branch when `rs1 == rs2` |
| `BNE` | opcode `0x63`, funct3 `1` | branch when `rs1 != rs2` |
| `LW` | opcode `0x03`, funct3 `2` | `rd = mem32[rs1 + sext(imm12)]` |
| `LBU` | opcode `0x03`, funct3 `4` | `rd = zero_extend(mem8[rs1 + sext(imm12)])` |
| `SW` | opcode `0x23`, funct3 `2` | `mem32[rs1 + sext(s_imm12)] = rs2` |
| `SB` | opcode `0x23`, funct3 `0` | `mem8[rs1 + sext(s_imm12)] = rs2[7:0]` |
| `JALR` | opcode `0x67`, funct3 `0` | `rd = old_pc + 4; pc = (old_rs1 + sext(imm12)) & ~1` |
| `EBREAK` | exactly `0x00100073` | retire and terminate successfully |

`LW` and `SW` require four-byte-aligned data addresses. `LBU` and `SB` have no
data-alignment restriction. JALR reads its source before writing its destination,
so `jalr x1, imm(x1)` behaves correctly.

Taken branch targets must be four-byte aligned and otherwise raise instruction-
address misalignment. Not-taken branches always advance by four and do not trap
on the alignment of their encoded target.

## Execution and errors

`step()` executes at most one instruction. `run(limit)` stops on `EBREAK`, the
first error, or after `limit` instructions. Exhausting the bound produces
`ExecutionLimit`; it is not a successful halt.

Errors have deterministic status values and diagnostic strings:

- `IllegalInstruction` for unsupported encodings or invalid RV32E registers
- `MemoryError` for instruction or data accesses outside allocated memory
- `MisalignedAccess` for instruction fetches or `LW`/`SW` at invalid alignment
- `ExecutionLimit` when bounded execution does not reach `EBREAK`

An instruction that produces an error is not retired and is not added to the
trace. Every retired instruction, including `EBREAK`, adds a trace entry holding
its original PC, instruction word, and next PC.

## Building and testing

Run `make build` to compile and `make test` to execute the unit suite. The test
program returns a nonzero process status if any assertion fails. `make clean`
removes generated build artifacts.
