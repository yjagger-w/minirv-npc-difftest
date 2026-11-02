# Run the two explicit PYNQ-Z2 8ns OOC evaluations into new output directories.
# Args: SOURCE_ROOT NEW_OUTPUT_ROOT
if {[llength $argv] != 2} { error {Expected SOURCE_ROOT NEW_OUTPUT_ROOT} }
lassign $argv source_root output_root
set source_root [file normalize $source_root]
set output_root [file normalize $output_root]
if {[file exists $output_root]} { error "Use a new output root: $output_root" }
set ooc_flow [file join [file dirname [file normalize [info script]]] vivado_ooc.tcl]
foreach selected_top {minirv_core minirv_soc} {
    set argv [list $source_root $selected_top xc7z020clg400-1 8.0 [file join $output_root ${selected_top}-8ns]]
    if {[catch {source $ooc_flow} message options]} {
        puts stderr $message
        puts stderr [dict get $options -errorinfo]
        exit 1
    }
}
puts {BOTH_OOC_BASELINES_COMPLETE}
exit 0
