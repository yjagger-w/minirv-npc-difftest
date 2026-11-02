# Windows Vivado non-project, out-of-context flow. No device/clock defaults.
# Args: RTL_ROOT TOP PART CLOCK_PERIOD_NS NEW_OUTPUT_DIRECTORY
if {[llength $argv] != 5} {
    error {Usage: vivado -mode batch -source vivado_ooc.tcl -tclargs RTL_ROOT TOP PART CLOCK_PERIOD_NS NEW_OUTPUT_DIRECTORY}
}
lassign $argv root top part period out
set root [file normalize $root]
set out [file normalize $out]
if {$top ni {minirv_core minirv_soc}} { error "Unsupported top: $top" }
if {![string is double -strict $period] || $period <= 0 || $period >= 1000000} {
    error {CLOCK_PERIOD_NS must be an explicit positive finite number below 1000000}
}
if {![regexp {^[a-zA-Z0-9-]+$} $part] || [llength [get_parts -quiet $part]] != 1} {
    error "Exact installed PART required: $part"
}
set sources {}
foreach name {minirv_regfile minirv_decoder minirv_immgen minirv_alu minirv_core} {
    lappend sources [file join $root rtl vsrc ${name}.v]
}
if {$top eq "minirv_soc"} {
    foreach path {rtl/soc/minirv_bus.v rtl/peripheral/minirv_gpio.v rtl/peripheral/minirv_uart_tx.v rtl/soc/minirv_soc.v} {
        lappend sources [file join $root $path]
    }
}
foreach source $sources { if {![file isfile $source]} { error "Missing RTL: $source" } }
if {[file exists $out]} { error "Use a new output directory: $out" }
file mkdir $out
cd $out
set f [open run-settings.txt w]
puts $f [version]
puts $f "RTL_ROOT=$root\nTOP=$top\nPART=$part\nCLOCK_PERIOD_NS=$period"
puts $f {SCOPE=OOC post-synthesis estimate; no FPGA memory, board pins or external I/O timing model.}
close $f
create_project -in_memory -part $part
read_verilog $sources
set f [open core-clock.xdc w]
puts $f [format {create_clock -name core_clk -period %s [get_ports clk]} $period]
close $f
read_xdc core-clock.xdc
synth_design -top $top -part $part -mode out_of_context
report_utilization -file utilization.rpt
report_clocks -file clocks.rpt
check_timing -verbose -file check-timing.rpt
report_timing_summary -report_unconstrained -file timing-summary.rpt
report_timing -max_paths 10 -path_type full_clock_expanded -file critical-paths.rpt
set metrics [open metrics.txt w]
puts $metrics "TOP=$top\nPART=$part\nCLOCK_PERIOD_NS=$period"
puts $metrics "REGISTERS=[llength [all_registers]]"
set worst [get_timing_paths -max_paths 1 -nworst 1 -setup]
if {[llength $worst]} {
    puts $metrics "WORST_CONSTRAINED_SLACK_NS=[get_property SLACK $worst]"
    puts $metrics "STARTPOINT=[get_property STARTPOINT_PIN $worst]"
    puts $metrics "ENDPOINT=[get_property ENDPOINT_PIN $worst]"
}
close $metrics
write_checkpoint synthesized.dcp
puts {OOC_COMPLETE: Unconstrained interface paths and post-route timing still require board integration.}
close_project
