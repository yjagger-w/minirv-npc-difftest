# Args: staged repository root, unused output directory.
if {[llength $argv]!=2} { error {Expected SOURCE_ROOT NEW_OUTPUT_DIRECTORY} }
lassign $argv root out
set root [file normalize $root]
set out [file normalize $out]
if {[file exists $out]} { error "Refusing to overwrite $out" }
file mkdir $out
cd $root
create_project -in_memory -part xc7z020clg400-1
set_property target_language Verilog [current_project]
set sources {}
foreach f {minirv_regfile minirv_decoder minirv_immgen minirv_alu minirv_core} {
    lappend sources [file join $root rtl vsrc $f.v]
}
foreach f {rtl/soc/minirv_bus.v rtl/peripheral/minirv_gpio.v rtl/peripheral/minirv_uart_tx.v rtl/soc/minirv_soc.v} {
    lappend sources [file join $root $f]
}
read_verilog $sources
read_verilog -sv [list fpga/board/minirv_fpga_memory.sv fpga/board/minirv_fpga_system.sv fpga/board/minirv_pynq_z2.sv]
read_xdc fpga/board/pynq_z2_board.xdc
synth_design -top minirv_pynq_z2 -part xc7z020clg400-1
# Pin-level asynchronous controls only: verify their complete fanout before cuts.
# btn_reset solely requests async reset; no functional data path is exempted.
set btn_endpoints [all_fanout -flat -endpoints_only -from [get_ports btn_reset]]
foreach endpoint $btn_endpoints {
    set pin [get_property REF_PIN_NAME $endpoint]
    if {$pin ni {RST PRE CLR}} { error "Unexpected reset data endpoint $endpoint ($pin)" }
}
set reset_regs [get_cells -hier -filter {ASYNC_REG == TRUE}]
if {[llength $reset_regs]!=3} { error "Expected 3 reset-release synchronizer FFs: $reset_regs" }
set reset_async_pins [get_pins -of_objects $reset_regs -filter {REF_PIN_NAME == PRE || REF_PIN_NAME == CLR}]
if {[llength $reset_async_pins]!=3} { error {Reset pin collection mismatch} }
# These three asynchronous controls implement async-assert / sync-release.
# Only their recovery/removal arcs are exempted. D/CE paths remain timed.
# MMCM RST and LOCKED are dedicated primitive controls, not ordinary timing
# endpoints/startpoints; do not create invalid pin-to-pin exceptions for them.
set_false_path -to $reset_async_pins
set cpu_clock [get_clocks -of_objects [get_pins clocking_unit/cpu_global/O]]
if {[llength $cpu_clock]!=1 || abs([get_property PERIOD $cpu_clock]-20.0)>0.001} { error "Unexpected generated CPU clock $cpu_clock" }
set led_launch_pins [get_pins -of_objects [all_registers -clock $cpu_clock] -filter {REF_PIN_NAME == C}]
if {![llength $led_launch_pins]} { error {No valid LED launch register clock pins} }
set_max_delay -datapath_only 20.0 -from $led_launch_pins -to [get_ports {led[*]}]
# Fail before implementation if any async indicator budget was ignored.
foreach port [get_ports {led[*]}] {
    set p [get_timing_paths -to $port -delay_type max -max_paths 1]
    set slack [get_property SLACK $p]
    if {![string is double -strict $slack] || abs($slack)>1000000} { error "Invalid LED constraint before place: $port $slack" }
}
set fd [open [file join $out clock-and-reset-audit.txt] w]
puts $fd "PRIMARY=[get_clocks board_ref_clk] PERIOD_NS=8"
puts $fd "CPU_CLOCK=$cpu_clock PERIOD_NS=[get_property PERIOD $cpu_clock]"
puts $fd "BUTTON_FANOUT_AUDIT=$btn_endpoints"
puts $fd "ASYNC_RESET_EXCEPTION_PINS=$reset_async_pins"
puts $fd {LED_MAX_DATAPATH_BUDGET_NS=20; no synchronous receiver or hold requirement}
close $fd
report_utilization -hierarchical -file [file join $out utilization-synth.rpt]
report_timing_summary -report_unconstrained -file [file join $out timing-synth.rpt]
write_checkpoint [file join $out synthesized.dcp]
opt_design
place_design
phys_opt_design
route_design
report_route_status -file [file join $out route-status.rpt]
set audit [open [file join $out led-coverage.txt] w]
foreach port [get_ports {led[*]}] {
    set p [get_timing_paths -to $port -delay_type max -max_paths 1]
    if {![llength $p]} { error "No constrained LED path: $port" }
    set slack [get_property SLACK $p]
    if {![string is double -strict $slack] || abs($slack)>1000000} { error "Unconstrained LED: $port $slack" }
    puts $audit "$port REQUIREMENT_NS=[get_property REQUIREMENT $p] MAX_SLACK_NS=$slack"
}
close $audit
report_utilization -hierarchical -file [file join $out utilization-route.rpt]
report_timing_summary -delay_type min_max -report_unconstrained -max_paths 10 -file [file join $out timing-route.rpt]
report_timing -delay_type max -max_paths 10 -path_type full_clock_expanded -file [file join $out setup-critical.rpt]
report_timing -delay_type min -max_paths 10 -path_type full_clock_expanded -file [file join $out hold-critical.rpt]
report_clocks -file [file join $out clocks.rpt]
report_clock_utilization -file [file join $out clock-utilization.rpt]
check_timing -verbose -file [file join $out check-timing.rpt]
report_exceptions -file [file join $out exceptions.rpt]
report_cdc -details -file [file join $out cdc.rpt]
report_drc -file [file join $out drc.rpt]
write_checkpoint [file join $out routed.dcp]
write_xdc [file join $out effective-constraints.xdc]
set fd [open [file join $out metrics.txt] w]
puts $fd "PART=xc7z020clg400-1\nCPU_PERIOD_NS=20\nREFERENCE_PERIOD_NS=8"
puts $fd "LUTRAM_CELLS=[llength [get_cells -hier -filter {REF_NAME =~ RAM* && REF_NAME !~ RAMB*}]]"
puts $fd "BRAM_CELLS=[llength [get_cells -hier -filter {REF_NAME =~ RAMB*}]]"
puts $fd "MMCM_CELLS=[llength [get_cells -hier -filter {REF_NAME =~ MMCME2*}]]"
puts $fd "BUFG_CELLS=[llength [get_cells -hier -filter {REF_NAME == BUFG}]]"
foreach mode {max min} {
    set p [get_timing_paths -delay_type $mode -max_paths 1]
    puts $fd "WORST_${mode}_SLACK_NS=[get_property SLACK $p]"
    puts $fd "${mode}_START=[get_property STARTPOINT_PIN $p]"
    puts $fd "${mode}_END=[get_property ENDPOINT_PIN $p]"
}
close $fd
puts "BOARD_ROUTE_COMPLETE=$out"
close_project
exit 0
