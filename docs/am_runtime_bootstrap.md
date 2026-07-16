# miniRV NPC AM runtime bootstrap

The E6A bootstrap runs raw `ARCH=minirv-npc` AbstractMachine images on the
Verilated miniRV core. It intentionally supports only the existing miniRV
instruction subset; RV32E binaries are not assumed to be compatible merely
because they were compiled with `-march=rv32e`.

## Toolchain

Use `riscv64-unknown-elf-` tools, never `riscv64-linux-gnu-`. The required
picolibc specs file is:

```text
PICOLIBC_SPECS=/usr/lib/picolibc/riscv64-unknown-elf/picolibc.specs
```

Set `AM_HOME` to the AbstractMachine checkout used for the build. For this
workspace:

```sh
export AM_HOME=/home/chzione/projects/abstract-machine
```

The port compiles for `-march=rv32e -mabi=ilp32e` with PIC/PIE, builtins,
small-data addressing, and linker relaxation disabled. It links freestanding
with no standard library or startup files.

## Memory and termination

- Guest memory: `0x80000000` through `0x87ffffff` (128 MiB)
- Raw binary load address and reset PC: `0x80000000`
- ELF entry: `0x80000000`
- Fixed `main` address: `0x80000020`
- `EBREAK` with `a0 == 0`: `HIT GOOD TRAP`, process exit 0
- `EBREAK` with `a0 != 0`: `HIT BAD TRAP`, nonzero process exit
- Architectural traps, invalid memory accesses, and timeout also exit nonzero

The final linked-image whitelist is `ADD`, `SUB`, `ADDI`, `LUI`, `AUIPC`, `SLTIU`,
`BEQ`, `BNE`, `LW`, `LBU`, `SW`, `SB`, `JALR`, and `EBREAK`. The AM build
disassembles with aliases disabled and rejects any other linked instruction.

## Build and run

From `am-kernels/tests/cpu-tests`:

```sh
make ARCH=minirv-npc ALL=dummy
make ARCH=minirv-npc ALL=dummy run
make ARCH=minirv-npc ALL=wrong run
```

If `AM_HOME` is not exported, append
`AM_HOME=/home/chzione/projects/abstract-machine` to these commands.

Direct runner usage and the public NPC Makefile interface are:

```sh
/home/chzione/projects/minirv-npc-difftest/build/am/Vminirv_core \
  build/dummy-minirv-npc.bin --max-cycles 100000

make am-run IMAGE=/absolute/path/program.bin
make am-wave IMAGE=/absolute/path/program.bin
```

`--max-cycles` defaults to 100000. `--fst` produces
`build/minirv_am.fst`; the Makefile `am-wave` target enables it automatically.

## Dependency revisions

- abstract-machine: `9a44d8587c21799e4c863811e78ffce8058f0a28`
- am-kernels: `1600e123e48e0c672a0e6e2aa24ea9df6ee191a8`

Both dependencies are maintained as separate sibling Git repositories and
are not vendored into this repository.
