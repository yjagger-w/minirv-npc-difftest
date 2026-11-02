# Derived only from PYNQ-Z2 v1.0 Master XDC clock lines 8-9.
# Source SHA256: 07441e999bac956c77e78091c782cade278efc5a78affc51e5b12b4800b1fe73
# Top-level port renamed from vendor sysclk to miniRV clk.
# Board-wrapper integration only; OOC baseline uses a logical clock constraint.
# Other signals have no pin assignment until a real board wrapper is defined.
set_property -dict { PACKAGE_PIN H16 IOSTANDARD LVCMOS33 } [get_ports {clk}]
create_clock -name board_ref_clk -period 8.000 -waveform {0.000 4.000} [get_ports {clk}]
