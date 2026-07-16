# E7-A minimal simulated SoC

E7-A wraps the unchanged miniRV CPU in a minimal one-master SoC containing a
SimpleBus-style address decoder, a simulation-backed RAM target, and a 32-bit
GPIO peripheral. No instruction or CPU pipeline behavior changes in this
milestone.

## Block structure

```text
 external instruction memory -> miniRV core
                                  |
                                  | dmem_valid/write/addr/wdata/wmask/rdata
                                  v
                            minirv_bus
                              /     \
                 simulated RAM     minirv_gpio
                                      |     |
                                gpio_input gpio_output
```

`minirv_soc` preserves the core's external instruction-memory interface and
the parameterized `RESET_PC`. The data-memory signals form the initial simple
bus; there is one master, combinational decode, and no ready signal or stalls.
The decoded RAM port is backed by the Verilator test memory so program loading
remains compatible with the existing raw-memory model.

## Address map

| Range/address | Target | Access |
|---|---|---|
| `0x80000000`–`0x87ffffff` | RAM | read/write |
| `0x10000000` | GPIO input register | read-only |
| `0x10000004` | GPIO output register | read/write |

Byte addresses within each GPIO word select its corresponding byte lane.
Any valid data request outside these regions asserts `bus_error`; it is never
silently routed to RAM. The simulation test treats this indication as a
deterministic failure condition.

## RAM and GPIO behavior

The RAM target retains the CPU's existing byte-addressed operations and write
masks. `LW`, `LBU`, `SW`, and `SB` therefore retain their previous behavior
through the decoder. The Verilator RAM backing applies each asserted mask bit
to the corresponding byte.

GPIO input is an external 32-bit value. Stores to it are ignored. GPIO output
is a 32-bit register reset to zero, supports full-word and individual-byte
writes, and can be read back. For byte stores, the bus converts the byte address
and CPU mask into the correct GPIO register lane, leaving all other bytes
unchanged.

## Integrated loopback program

The explicit instruction stream uses only the existing ISA:

1. form GPIO base `0x10000000` with `LUI`;
2. `LW` GPIO input;
3. `SW` the value to GPIO output;
4. `LW` GPIO output and compare it with `BEQ`;
5. place zero in `a0` and terminate with the existing `EBREAK` convention.

With `gpio_input = 0x0000005a`, the integrated result is:

```text
GPIO input:  0x0000005a
GPIO output: 0x0000005a
HIT GOOD TRAP
```

## Validation

- Existing emulator: 26/26.
- Existing RTL lint: pass.
- Existing standalone RTL: 27/27.
- Existing DiffTest: 36/36.
- SoC lint: pass.
- SoC tests: 6/6.
- Integrated SoC waveform test: 1/1.
- Waveform: `build/minirv_soc.fst`, generated and nonempty.

SoC coverage includes GPIO input reads, `0x12345678` full-word output writes,
output readback, individual `SB` byte lanes, ignored input writes, RAM access
through the decoder, and unmapped-address `bus_error` assertion.

## Current limitations

E7-A has no bus handshaking, CPU stalls, arbitration, multiple masters, UART,
timer, interrupt controller, cache, DDR, AXI, Wishbone, pipeline changes, FPGA
integration, or operating-system support. RAM is a Verilator simulation model;
no FPGA memory primitive or external-memory controller is introduced.
