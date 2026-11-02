# Offline generation only. Caller verifies the frozen DCP SHA256 first.
if {[llength $argv]!=2} { error {Expected ROUTED_DCP NEW_BITSTREAM_PATH} }
lassign $argv dcp bitfile
if {[file exists $bitfile]} { error {Refusing to overwrite a bitstream} }
open_checkpoint $dcp
if {[get_property PART [current_design]] ne "xc7z020clg400-1"} {error {Unexpected device}}
# Keep the original rule enabled and its original severity. No PS init is run.
report_drc -checks [get_drc_checks ZPS7-1] -file "${bitfile}.ZPS7-1.rpt"
write_bitstream $bitfile
close_design
exit
