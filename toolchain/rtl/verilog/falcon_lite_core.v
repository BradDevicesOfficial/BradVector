// SPDX-License-Identifier: MIT
// Falcon-Lite 3-stage ultra-tiny core
// Stages: F -> D -> EX (execute + writeback)
// 8 registers (r0-r7), single-issue, in-order, no multiplier
// Target: < 500 LUTs

`include "bradisa_defines.v"

module falcon_lite_core (
    input  wire         clk,
    input  wire         rst_n,
    output wire [31:0]  imem_addr,
    input  wire [31:0]  imem_rdata,
    output wire [31:0]  dmem_addr,
    output wire         dmem_req,
    output wire         dmem_we,
    output wire [31:0]  dmem_wdata,
    input  wire [31:0]  dmem_rdata
);

    // ─── Core parameters ─────────────────────────────────────────
    localparam FALCON_NUM_REGS = 8;

    // ─── Embedded register file (8 x 32-bit, async read) ─────────
    reg [31:0] regs [0:FALCON_NUM_REGS-1];

    // ─── Pipeline registers ──────────────────────────────────────
    // Fetch stage
    reg         f_valid;
    reg [31:0]  f_pc;
    reg [31:0]  f_insn;

    // Decode stage
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
    reg         d_mem_req;
    reg         d_mem_we;

    // Execute/Writeback stage
    reg         e_valid;
    reg [3:0]   e_rd;
    reg [31:0]  e_result;
    reg [31:0]  e_addr;
    reg         e_reg_we;
    reg         e_mem_req;
    reg         e_mem_we;
    reg [31:0]  e_store_data;

    // ─── PC ──────────────────────────────────────────────────────
    reg [31:0] pc;
    wire [31:0] next_pc;
    wire        branch_taken;

    // ─── Fetch ───────────────────────────────────────────────────
    assign imem_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc      <= 32'd0;
            f_valid <= 1'b0;
            f_pc    <= 32'd0;
            f_insn  <= 32'd0;
        end else if (branch_taken) begin
            f_valid <= 1'b0;
            pc      <= next_pc;
        end else begin
            f_valid <= 1'b1;
            f_pc    <= pc;
            f_insn  <= imem_rdata;
            pc      <= pc + 32'd4;
        end
    end

    // ─── Decode ─────────────────────────────────────────────────
    wire [3:0]  raw_op  = f_insn[BRAD_OPCODE_SHIFT +: 4];
    wire [3:0]  raw_rd  = f_insn[BRAD_RD_SHIFT     +: 4];
    wire [3:0]  raw_rs1 = f_insn[BRAD_RS1_SHIFT    +: 4];
    wire [3:0]  raw_rs2 = f_insn[BRAD_RS2_SHIFT    +: 4];
    wire [15:0] raw_imm = f_insn[15:0];

    // Register file read (async)
    wire [31:0] rf_rdata1 = (raw_rs1 >= FALCON_NUM_REGS) ? 32'd0 :
                            (raw_rs1 == 4'd0) ? 32'd0 : regs[raw_rs1];
    wire [31:0] rf_rdata2 = (raw_rs2 >= FALCON_NUM_REGS) ? 32'd0 :
                            (raw_rs2 == 4'd0) ? 32'd0 : regs[raw_rs2];

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
            d_mem_req <= 1'b0;
            d_mem_we  <= 1'b0;
        end else if (branch_taken) begin
            d_valid <= 1'b0;
        end else begin
            d_valid   <= f_valid;
            d_pc      <= f_pc;
            d_opcode  <= raw_op;
            d_rd      <= raw_rd;
            d_rs1     <= raw_rs1;
            d_rs2     <= raw_rs2;
            d_rs1_val <= rf_rdata1;
            d_rs2_val <= rf_rdata2;
            d_imm     <= { {16{raw_imm[15]}}, raw_imm };

            // Writeback enable for ALU ops, ADDI, LDW (except MUL)
            d_reg_we  <= ((raw_op <= BRAD_OP_ADDI) && (raw_op != BRAD_OP_MUL)) ||
                         (raw_op == BRAD_OP_LDW);

            // Memory request for LDW/STW
            d_mem_req <= (raw_op == BRAD_OP_LDW) || (raw_op == BRAD_OP_STW);
            d_mem_we  <= (raw_op == BRAD_OP_STW);
        end
    end

    // ─── Execute + Writeback ─────────────────────────────────────
    wire [3:0]  e_opcode = d_opcode;
    reg  [31:0] alu_result;

    // ALU: all ops except MUL (raises exception)
    wire [31:0] alu_a = d_rs1_val;
    wire [31:0] alu_b = (e_opcode == BRAD_OP_ADDI) ? d_imm : d_rs2_val;

    always @(*) begin
        case (e_opcode)
            BRAD_OP_ADD: alu_result = alu_a + alu_b;
            BRAD_OP_SUB: alu_result = alu_a - alu_b;
            BRAD_OP_MUL: alu_result = 32'd0;  // MUL not implemented
            BRAD_OP_AND: alu_result = alu_a & alu_b;
            BRAD_OP_OR:  alu_result = alu_a | alu_b;
            BRAD_OP_XOR: alu_result = alu_a ^ alu_b;
            BRAD_OP_SHL: alu_result = alu_a << alu_b[4:0];
            BRAD_OP_SHR: alu_result = alu_a >> alu_b[4:0];
            BRAD_OP_ADDI: alu_result = alu_a + d_imm;
            default:     alu_result = 32'd0;
        endcase
    end

    // MUL exception: set exception code
    reg mul_exception;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            mul_exception <= 1'b0;
        else if (e_valid && (e_opcode == BRAD_OP_MUL))
            mul_exception <= 1'b1;
        else
            mul_exception <= 1'b0;
    end

    // Branch resolution
    wire bz_taken   = d_valid && (e_opcode == BRAD_OP_BZ)  && (d_rs1_val == 32'd0);
    wire bnz_taken  = d_valid && (e_opcode == BRAD_OP_BNZ) && (d_rs1_val != 32'd0);
    wire jmp_taken  = d_valid && (e_opcode == BRAD_OP_JMP);
    wire call_taken = d_valid && (e_opcode == BRAD_OP_CALL);
    wire ret_taken  = d_valid && (e_opcode == BRAD_OP_RET);
    assign branch_taken = (bz_taken | bnz_taken | jmp_taken | call_taken | ret_taken);

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
        end else begin
            e_valid   <= d_valid && !branch_taken;
            e_rd      <= d_rd;
            e_reg_we  <= d_reg_we;

            // ALU/ADDI result (all ops except MUL)
            if (e_opcode != BRAD_OP_MUL && e_opcode <= BRAD_OP_ADDI)
                e_result <= alu_result;
            else
                e_result <= 32'd0;

            // CALL: save PC+4 to link register
            if (e_opcode == BRAD_OP_CALL) begin
                e_result <= d_pc + 32'd4;
                e_rd     <= BRAD_LR;
                e_reg_we <= 1'b1;
            end

            // RET: rs1_val is the return address
            // (handled by next_pc computation above)

            // Memory access
            e_addr       <= d_rs1_val + d_imm;
            e_store_data <= d_rs2_val;
            e_mem_req    <= d_mem_req;
            e_mem_we     <= d_mem_we;
        end
    end

    // ─── Register file write (synchronous) ───────────────────────
    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < FALCON_NUM_REGS; i = i + 1)
                regs[i] <= 32'd0;
        end else if (e_valid && e_reg_we && (e_rd != 4'd0) && (e_rd < FALCON_NUM_REGS)) begin
            // LDW: load from memory, else ALU result
            if (e_mem_req && !e_mem_we)
                regs[e_rd] <= dmem_rdata;
            else
                regs[e_rd] <= e_result;
        end
    end

    // ─── Outputs ─────────────────────────────────────────────────
    assign dmem_addr  = e_addr;
    assign dmem_req   = e_mem_req;
    assign dmem_we    = e_mem_we;
    assign dmem_wdata = e_store_data;

endmodule
