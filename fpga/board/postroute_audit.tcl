if {[llength $argv]!=1} { error {Expected IMPLEMENTATION_DIRECTORY} }
set out [file normalize [lindex $argv 0]]
open_checkpoint [file join $out routed.dcp]
report_utilization -file [file join $out utilization-summary.rpt]
report_timing -to [get_ports {led[*]}] -delay_type max -max_paths 4 -file [file join $out led-max-paths.rpt]
set f [open [file join $out memory-primitives.txt] w]
set counts [dict create]
foreach cell [get_cells -quiet -hier -filter {IS_PRIMITIVE == 1 && REF_NAME =~ RAM*}] {
    dict incr counts [get_property REF_NAME $cell]
}
dict for {name count} $counts { puts $f "$name=$count" }
puts $f {Use utilization-summary.rpt LUT-as-memory count for physical LUT resource accounting.}
close $f
write_verilog -mode funcsim [file join $out board-functional.v]
puts {POST_ROUTE_AUDIT_EXPORTED}
close_design
exit
