# Board RTL and memory contract

The board top is `minirv_pynq_z2`. It instantiates the unchanged `minirv_soc`,
configuration-initialized asynchronous ROM/RAM, an MMCM/global clock network,
three-stage reset release and four status LEDs. See [the FPGA entry point](../README.md)
for complete build commands, results, bitstream hash and hardware preconditions.

## Memory and retirement

Default ROM and RAM capacities are each 256x32 bits (1KiB). Word counts must be
powers of two >=2; the supplied 31-word image needs >=32 ROM words. The tested
address maps do not wrap around 32 bits: ROM starts at zero, RAM at 0x80000000.
The SoC's larger RAM decode window does not imply that capacity is implemented.

PC/ROM output and RAM reads settle combinationally during the current cycle.
At the next rising edge, the core retires and a store updates RAM from the
pre-edge RAM request. Registered commit_mem outputs are not used to issue stores.
The following load observes updated memory in its cycle. There is no inserted
memory latency or hidden ready/stall protocol. Synchronous BRAM would require
an explicit wait/response/retirement design; it cannot replace this RAM directly.

SW/LW use the core's alignment checks. For SB, mask bit0 refers to the effective
byte address. The adapter shifts data/enables into physical lanes; read values
are shifted down for LBU. Non-crossing partial masks are retained. Invalid RAM
addresses do not alias or write valid storage; crossing writes raise a board
fault. These faults are diagnostics, not new precise architectural access traps.
Misaligned/out-of-range ROM fetch returns zero and faults; unused ROM is illegal zero.

RAM INIT values are zero at FPGA configuration. Soft reset inhibits writes but
preserves RAM; software must initialize what it needs. The smoke program rewrites
its test word on every boot. This keeps actual LUTRAM inference (128 physical
LUTRAM LUTs for the default RAM); ROM constants become LUT logic. No BRAM is used.
Changing the fixed ROM requires fresh simulation/synthesis/implementation.

## Clock/reset and ports

| Port | Pin | Role |
|---|---|---|
| clk125 | H16 | 125MHz reference, LVCMOS33 |
| btn_reset | D19 | active-high BTN0 |
| led[0] | R14 | latched EBREAK completion |
| led[1] | P14 | latched trap/bus/fetch/RAM fault |
| led[2] | N16 | GPIO bit0 (A5 signature gives high) |
| led[3] | M14 | CPU heartbeat |

IBUF -> MMCME2_BASE (125MHz x8 /1 /20) -> BUFG gives 50MHz. Feedback uses a
second BUFG. Vivado auto-derives the actual 20ns clock; no fabric clock division.
BTN0 or !LOCKED asserts the reset-release chain asynchronously; release takes
three CPU edges. INIT=1 and ASYNC_REG/SHREG_EXTRACT attributes retain its intent.
Core reset is synchronous: if CPU clock stops, state clears on resumed edges.
BTN0 resets the MMCM; !LOCKED alone does not. After reference loss, restore a
stable reference and pulse BTN0; automatic lost-lock restart is not promised.

Only these six signals reach package pins. Internal debug/commit/bus ports remain
inside the system. UART is still the SoC byte-event interface, not a serial PHY.

## Constraints and simulation

Only used pin assignments are enabled. The script validates the MMCM clock and
button fanout. Async exceptions target exactly the three reset FF PRE/CLR pins;
synchronizer D/CE and all functional paths remain timed. Four LED paths have
explicit register-clock-pin-to-pad max-datapath budgets of 20ns, checked for finite
slack before and after route. LEDs have no receiving clock or hold requirement.
No CPU false path, multicycle or blanket clock-group exception hides failures.

Final check_timing: no_clock=0, unconstrained_internal_endpoints=0,
no_output_delay=0; one no_input_delay warning for intentionally asynchronous
BTN0. Unconstrained paths in the report are the documented BTN0/LOCKED -> PRE
controls. CDC-9 identifies the three-stage synchronized asynchronous reset.

`make -f fpga/board/board.mk board-lint board-test` tests ROM fetch/alignment/bounds,
RAM words/all byte lanes/partial masks/boundaries/reset retention/edge ordering,
30 commits with six loads/six stores, GPIO A5/EBREAK, no post-halt commits,
reset replay/three-edge release, and an illegal-instruction fault.
Verilator explicitly models clock frequency/lock only. `xsim.ps1` instead uses
UNISIM MMCM/BUFG/IBUF. The routed functional test verifies initialized RAM/ROM,
startup and reset replay; no SDF is applied, so STA supplies setup/hold evidence.
Neither simulator establishes physical oscillator, board power or PS state.

## Hardware limits

JTAG programming succeeded on 2026-11-02. Initial smoke LEDs and BTN0 replay,
and the same checks after full power removal and JTAG reload, passed according
to user observations. Video additionally supports the normal LEDs and about 3Hz
heartbeat; it does not independently establish the complete BTN0/power-cycle
sequence. See [hardware validation](hardware-validation-20261102.md) for evidence
levels and remaining unknowns. The 125MHz reference was not instrument-measured.
ZPS7-1 remains Warning because there is no PS7 instance/default-configuration
design; it is not waived.
The 125MHz input comes from the Ethernet PHY and disappears if PHYRSTB is low.
Confirm board revision and PS/PHY boot conditions before download; no PS register
writes or unreviewed initialization scripts are supplied.

Observe at least two seconds: LED0/2 on, LED1 off, LED3 about 2.98Hz; BTN0 release
should reproduce that pattern. During button hold, MMCM clock may stop and LEDs
can retain state until CPU clock returns. A heartbeat implies running clock/reset
release; without ILA, no heartbeat cannot distinguish lock and reset failures.
After power loss the JTAG configuration must be reloaded; no autonomous boot image
is provided. Preserve the original OOC estimates separately; scope/constant
propagation and debug pruning mean resource differences are not optimization gains.
