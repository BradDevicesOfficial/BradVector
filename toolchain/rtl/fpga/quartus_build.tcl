# BradISA V1 -- Quartus Prime build script
# Usage: quartus_sh -t build.tcl

set project_name brad_core
set device_family "Cyclone V"
set device "5CSXFC6D6F31C6ES"

# Create project
load_package flow
project_new $project_name -overwrite
set_global_assignment -name FAMILY $device_family
set_global_assignment -name DEVICE $device
set_global_assignment -name TOP_LEVEL_ENTITY brad_core

# Add sources
set_global_assignment -name VHDL_FILE ../vhdl/bradisa_pkg.vhd
set_global_assignment -name VHDL_FILE ../vhdl/brad_regfile.vhd
set_global_assignment -name VHDL_FILE ../vhdl/brad_alu.vhd
set_global_assignment -name VHDL_FILE ../vhdl/brad_core.vhd

# Add constraints
set_global_assignment -name SDC_FILE brad_core.sdc

# Compile
execute_flow -compile

# Report
report_timing -panel_name "Timing" -file ./timing.rpt
report_utilization -panel_name "Utilization" -file ./util.rpt

project_close
