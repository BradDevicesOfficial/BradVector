# BradISA V1 -- Vivado build script (build.tcl)
# Usage: vivado -mode batch -source build.tcl

set project_name brad_core
set part xc7a35ticsg324-1L  ;# Artix-7 35T (Nexys Video / Arty)
set top_module brad_core

# Create project
create_project -force $project_name ./$project_name -part $part

# Add sources
add_files -fileset sim_1 -norecurse {
    ../verilog/tb_brad_core.v
}
add_files -norecurse {
    ../verilog/brad_core.v
    ../verilog/brad_regfile.v
    ../verilog/brad_alu.v
    ../verilog/bradisa_defines.v
}

# Add constraints
add_files -fileset constrs_1 -norecurse ./brad_core.xdc

# Set top
set_property top $top_module [current_fileset]

# Run synthesis
synth_design -top $top_module -part $part
report_timing -max_paths 10 -file ./timing_post_synth.rpt
report_utilization -file ./util_post_synth.rpt

# Place and route
opt_design
place_design
route_design
report_timing -max_paths 10 -file ./timing_post_route.rpt
report_utilization -file ./util_post_route.rpt

# Generate bitstream
write_bitstream -force ./${project_name}.bit

puts "Build complete: ${project_name}.bit"
