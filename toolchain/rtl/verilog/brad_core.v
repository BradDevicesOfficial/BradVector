// SPDX-License-Identifier: MIT
// BradISA V1 -- BradCore (Falcon 5-stage pipeline)
// Synthesisable RTL for FPGA. Stages: F1→F2→D→EX→WB
// Simple in-order, single-issue, no forwarding (stall on RAW hazards).

`include "bradisa_defines.v"

module brad_core (
    input  wire         clk,
    input  wire         rst_n,
    // Instruction memory port (single-cycle)
    output wire [31:0]  imem_addr,
    input  wire [31:0]  imem_rdata,
    // Data memory port (single-cycle)
    output wire [31:0]  dmem_addr,
    output wire         dmem_req,
    output wire         dmem_we,
    output wire [31:0]  dmem_wdata,
    input  wire [31:0]  dmem_rdata
);

    // ─── Pipeline registers ────────────────────────────────────
    // Stage 1: Fetch
    reg         f_valid;
    reg [31:0]  f_pc;
    reg [31:0]  f_insn;
    // Stage 2: Decode
    reg         d_valid;
    reg [31:0]  d_pc;
    reg [3:0]   d_opcode;
    reg [3:0]   d_rd;
    reg [3:0]   d_rs1;
    reg [3:0]   d_rs2;
    reg [31:0]  d_rs1_val;
    reg [31:0]  d_rs2_val;
    reg [31:0]  d_imm;
    reg         d_reg_we;
    // Stage 3: Execute+Writeback (combined)
    reg         e_valid;
    reg [3:0]   e_rd;
    reg [31:0]  e_result;
    reg [31:0]  e_addr;
    reg         e_reg_we;
    reg         e_mem_req;
    reg         e_mem_we;
    reg [31:0]  e_store_data;

    // ─── PC ────────────────────────────────────────────────────
    reg [31:0] pc;
    wire [31:0] next_pc;
    wire        branch_taken;

    // ─── Register file ─────────────────────────────────────────
    wire [3:0]  rf_raddr1 = d_rs1;
    wire [3:0]  rf_raddr2 = (d_opcode == BRAD_OP_STW) ? d_rs2 : 4'd0;
    wire [31:0] rf_rdata1;
    wire [31:0] rf_rdata2;
    wire        rf_we;
    wire [3:0]  rf_waddr;
    wire [31:0] rf_wdata;

    brad_regfile regfile (
        .clk(clk), .rst_n(rst_n),
        .raddr1(rf_raddr1), .rdata1(rf_rdata1),
        .raddr2(rf_raddr2), .rdata2(rf_rdata2),
        .we(rf_we), .waddr(rf_waddr), .wdata(rf_wdata)
    );

    // ─── Hazard detection ─────────────────────────────────────
    // RAW hazard: decode reads a register that EX is writing
    wire raw_hazard = d_valid && e_valid && e_reg_we && (e_rd != BRAD_R0) &&
                      ((d_rs1 == e_rd) || (d_opcode == BRAD_OP_STW && d_rs2 == e_rd));
    wire stall = raw_hazard;

    // ─── Fetch stage ──────────────────────────────────────────
    assign imem_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 32'd0;
            f_valid <= 1'b0;
            f_pc    <= 32'd0;
            f_insn  <= 32'd0;
        end else if (branch_taken) begin
            // Flush on branch
            f_valid <= 1'b0;
            pc      <= next_pc;
        end else if (!stall) begin
            f_valid <= 1'b1;
            f_pc    <= pc;
            f_insn  <= imem_rdata;
            pc      <= pc + 32'd4;
        end
    end

    // ─── Decode stage ─────────────────────────────────────────
    wire [3:0]  raw_op = f_insn[BRAD_OPCODE_SHIFT +: 4];
    wire [3:0]  raw_rd = f_insn[BRAD_RD_SHIFT     +: 4];
    wire [3:0]  raw_rs1 = f_insn[BRAD_RS1_SHIFT   +: 4];
    wire [3:0]  raw_rs2 = f_insn[BRAD_RS2_SHIFT   +: 4];
    wire [15:0] raw_imm = f_insn[15:0];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d_valid   <= 1'b0;
            d_pc      <= 32'd0;
            d_opcode  <= 4'd0;
            d_rd      <= 4'd0;
            d_rs1     <= 4'd0;
            d_rs2     <= 4'd0;
            d_rs1_val <= 32'd0;
            d_rs2_val <= 32'd0;
            d_imm     <= 32'd0;
            d_reg_we  <= 1'b0;
        end else if (branch_taken) begin
            d_valid <= 1'b0;
        end else if (!stall) begin
            d_valid   <= f_valid;
            d_pc      <= f_pc;
            d_opcode  <= raw_op;
            d_rd      <= raw_rd;
            d_rs1     <= raw_rs1;
            d_rs2     <= raw_rs2;
            d_rs1_val <= rf_rdata1;
            d_rs2_val <= rf_rdata2;
            d_imm     <= { {16{raw_imm[15]}}, raw_imm };
            // Writeback enable for ALU/ADDI/LDW (not STW/branches/JMP)
            d_reg_we  <= (raw_op <= BRAD_OP_ADDI) || (raw_op == BRAD_OP_LDW);
        end
    end

    // ─── Execute + Writeback stage ────────────────────────────
    wire [3:0] e_opcode = d_opcode;
    reg  [31:0] alu_result;

    // ALU
    wire [31:0] alu_a = d_rs1_val;
    wire [31:0] alu_b = d_rs2_val;
    always @(*) begin
        case (e_opcode)
            BRAD_OP_ADD: alu_result = alu_a + alu_b;
            BRAD_OP_SUB: alu_result = alu_a - alu_b;
            BRAD_OP_MUL: alu_result = alu_a * alu_b;
            BRAD_OP_AND: alu_result = alu_a & alu_b;
            BRAD_OP_OR:  alu_result = alu_a | alu_b;
            BRAD_OP_XOR: alu_result = alu_a ^ alu_b;
            BRAD_OP_SHL: alu_result = alu_a << alu_b[4:0];
            BRAD_OP_SHR: alu_result = alu_a >> alu_b[4:0];
            default:     alu_result = 32'd0;
        endcase
    end

    // Branch resolution
    wire bz_taken   = (e_opcode == BRAD_OP_BZ) && (d_rs1_val == 32'd0);
    wire bnz_taken  = (e_opcode == BRAD_OP_BNZ) && (d_rs1_val != 32'd0);
    wire jmp_taken  = (e_opcode == BRAD_OP_JMP);
    wire call_taken = (e_opcode == BRAD_OP_CALL);
    wire ret_taken  = (e_opcode == BRAD_OP_RET);
    assign branch_taken = d_valid && (bz_taken | bnz_taken | jmp_taken | call_taken | ret_taken);
    // BR/JMP/CALL target: PC+4 + imm (imm is already offset*4 in BR format)
    wire [31:0] br_target = d_pc + 32'd4 + d_imm;
    assign next_pc = ret_taken ? d_rs1_val : br_target;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            e_valid      <= 1'b0;
            e_rd         <= 4'd0;
            e_result     <= 32'd0;
            e_addr       <= 32'd0;
            e_reg_we     <= 1'b0;
            e_mem_req    <= 1'b0;
            e_mem_we     <= 1'b0;
            e_store_data <= 32'd0;
        end else if (!stall) begin
            e_valid   <= d_valid && !branch_taken;
            e_rd      <= d_rd;
            e_reg_we  <= d_reg_we;

            // ALU / ADDI result
            if (e_opcode <= BRAD_OP_SHR)
                e_result <= alu_result;
            else if (e_opcode == BRAD_OP_ADDI)
                e_result <= d_rs1_val + d_imm;
            else
                e_result <= 32'd0;

            // CALL: save PC+4 to link register
            if (e_opcode == BRAD_OP_CALL) begin
                e_result <= d_pc + 32'd4;
                e_rd     <= BRAD_LR;
                e_reg_we <= 1'b1;
            end

            // Memory access
            e_addr       <= d_rs1_val + d_imm;
            e_store_data <= d_rs2_val;
            e_mem_req    <= (e_opcode == BRAD_OP_LDW) || (e_opcode == BRAD_OP_STW);
            e_mem_we     <= (e_opcode == BRAD_OP_STW);
        end
    end

    // ─── Writeback to register file ───────────────────────────
    assign rf_we    = e_valid && e_reg_we && (e_rd != BRAD_R0);
    assign rf_waddr = e_rd;
    assign rf_wdata = (e_mem_req && !e_mem_we) ? dmem_rdata : e_result;

    // Memory port outputs
    assign dmem_addr  = e_addr;
    assign dmem_req   = e_mem_req;
    assign dmem_we    = e_mem_we;
    assign dmem_wdata = e_store_data;

endmodule
