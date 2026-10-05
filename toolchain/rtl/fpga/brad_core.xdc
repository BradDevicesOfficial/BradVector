# BradISA V1 -- Xilinx Vivado Constraints (brad_core.xdc)
# Clock
create_clock -period 10.000 -name sys_clk [get_ports clk]

# Input delays
set_input_delay -clock sys_clk -max 2.000 [get_ports imem_rdata*]
set_input_delay -clock sys_clk -min 0.500 [get_ports imem_rdata*]
set_input_delay -clock sys_clk -max 2.000 [get_ports dmem_rdata*]
set_input_delay -clock sys_clk -min 0.500 [get_ports dmem_rdata*]

# Output delays
set_output_delay -clock sys_clk -max 2.000 [get_ports imem_addr*]
set_output_delay -clock sys_clk -min 0.500 [get_ports imem_addr*]
set_output_delay -clock sys_clk -max 2.000 [get_ports dmem_addr*]
set_output_delay -clock sys_clk -min 0.500 [get_ports dmem_addr*]
set_output_delay -clock sys_clk -max 2.000 [get_ports dmem_wdata*]
set_output_delay -clock sys_clk -min 0.500 [get_ports dmem_wdata*]

# False paths (async resets)
set_false_path -from [get_ports rst_n]

# Clock groups (single clock domain)
set_clock_groups -name system -asynchronous -group [get_clocks sys_clk]

# Optimise for speed
set_property OPTIMIZE.PIPELINE_INSERTION 1 [current_design]
