# Derived from vendor PYNQ-Z2 v1.0 XDC. Only physically used pins are enabled.
# Vendor SHA256 07441e999bac956c77e78091c782cade278efc5a78affc51e5b12b4800b1fe73.
set_property -dict {PACKAGE_PIN H16 IOSTANDARD LVCMOS33} [get_ports clk125]
create_clock -name board_ref_clk -period 8.000 [get_ports clk125]
set_property -dict {PACKAGE_PIN D19 IOSTANDARD LVCMOS33} [get_ports btn_reset]
set_property -dict {PACKAGE_PIN R14 IOSTANDARD LVCMOS33} [get_ports {led[0]}]
set_property -dict {PACKAGE_PIN P14 IOSTANDARD LVCMOS33} [get_ports {led[1]}]
set_property -dict {PACKAGE_PIN N16 IOSTANDARD LVCMOS33} [get_ports {led[2]}]
set_property -dict {PACKAGE_PIN M14 IOSTANDARD LVCMOS33} [get_ports {led[3]}]
set_property SLEW SLOW [get_ports {led[*]}]
set_property DRIVE 4 [get_ports {led[*]}]
# MMCM output clocks are derived by Vivado from the real primitive, not invented.
# LEDs have no receiving clock. These explicit one-CPU-cycle datapath budgets
# cover register-to-pad paths without pretending there is a synchronous receiver.
# The implementation flow applies the 20 ns datapath-only budget after synthesis
# with explicit source-register clock pins (required by -datapath_only).
# No hold requirement exists for these asynchronous visible indicators.
