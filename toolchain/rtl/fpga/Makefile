# Makefile for BradISA V1 FPGA Builds

# Tools
VIVADO   ?= vivado
QUARTUS  ?= quartus_sh
GHDL     ?= ghdl
VERILATOR ?= verilator

# Directories
VERILOG_DIR = ../verilog
VHDL_DIR    = ../vhdl
FPGA_DIR    = .

# Sources (Verilog)
VERILOG_SRCS = \
    $(VERILOG_DIR)/bradisa_defines.v \
    $(VERILOG_DIR)/brad_regfile.v \
    $(VERILOG_DIR)/brad_alu.v \
    $(VERILOG_DIR)/brad_core.v \
    $(VERILOG_DIR)/tb_brad_core.v

# Sources (VHDL)
VHDL_SRCS = \
    $(VHDL_DIR)/bradisa_pkg.vhd \
    $(VHDL_DIR)/brad_regfile.vhd \
    $(VHDL_DIR)/brad_alu.vhd \
    $(VHDL_DIR)/brad_core.vhd \
    $(VHDL_DIR)/tb_brad_core.vhd

.PHONY: all vivado quartus lint_verilog lint_vhdl clean

all: lint_verilog lint_vhdl

# ─── Vivado (Xilinx) ───────────────────────────────────────────
vivado:
	cd $(FPGA_DIR) && $(VIVADO) -mode batch -source vivado_build.tcl

# ─── Quartus (Intel) ───────────────────────────────────────────
quartus:
	cd $(FPGA_DIR) && $(QUARTUS) -t quartus_build.tcl

# ─── Verilator lint ────────────────────────────────────────────
lint_verilog:
	$(VERILATOR) --lint-only -Wall $(VERILOG_SRCS)

# ─── GHDL lint / analyse ──────────────────────────────────────
lint_vhdl:
	cd $(VHDL_DIR) && $(GHDL) -s --std=08 bradisa_pkg.vhd brad_regfile.vhd brad_alu.vhd brad_core.vhd tb_brad_core.vhd

# ─── Clean ─────────────────────────────────────────────────────
clean:
	rm -rf $(FPGA_DIR)/brad_core $(FPGA_DIR)/*.rpt $(FPGA_DIR)/*.bit
	rm -rf $(VHDL_DIR)/*.o $(VHDL_DIR)/*.cf $(VHDL_DIR)/work-obj08.cf
	rm -rf $(VERILOG_DIR)/*.o
