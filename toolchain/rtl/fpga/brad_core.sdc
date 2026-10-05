# BradISA V1 -- Intel Quartus Prime SDC Constraints (brad_core.sdc)
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
