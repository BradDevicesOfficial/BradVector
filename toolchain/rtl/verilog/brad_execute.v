// SPDX-License-Identifier: MIT
// BradISA V1 -- Execute Stage
// ALU ops, address generation, branch resolution.

`include "bradisa_defines.v"

module brad_execute (
    input  wire         clk,
    input  wire         rst_n,
    // From decode
    input  wire         dec_valid,
    input  wire [3:0]   dec_opcode,
    input  wire [3:0]   dec_rd,
    input  wire [3:0]   dec_rs1,
    input  wire [3:0]   dec_rs2,
    input  wire [31:0]  dec_rs1_val,
    input  wire [31:0]  dec_rs2_val,
    input  wire [31:0]  dec_pc,
    input  wire [31:0]  dec_imm,
    input  wire         dec_reg_we,
    // Pipeline control
    input  wire         stall,
    input  wire         flush,
    // To fetch (branch redirect)
    output reg          branch_taken,
    output reg  [31:0]  branch_target,
    // To writeback
    output reg          ex_valid,
    output reg  [3:0]   ex_rd,
    output reg  [31:0]  ex_result,
    output reg  [31:0]  ex_store_data,
    output reg  [31:0]  ex_addr,
    output reg          ex_reg_we,
    output reg          ex_mem_req,   // memory operation requested
    output reg          ex_mem_we,    // 1 = store, 0 = load
    // Data memory interface
    input  wire [31:0]  dmem_rdata,
    input  wire         dmem_ready
);

    wire [3:0] opcode = dec_opcode;
    reg  [31:0] alu_result;
    reg         is_mem;
    reg         is_store;
    reg         is_branch;

    // ALU for opcodes ADD..SHR
    brad_alu alu_inst (
        .a(dec_rs1_val),
        .b(dec_rs2_val),
        .op(dec_opcode),
        .result(alu_result)
    );

    // Branch resolution
    wire bz_taken  = (opcode == BRAD_OP_BZ)  && (dec_rs1_val == 32'd0);
    wire bnz_taken = (opcode == BRAD_OP_BNZ) && (dec_rs1_val != 32'd0);
    wire jmp_taken = (opcode == BRAD_OP_JMP);
    wire call_taken = (opcode == BRAD_OP_CALL);
    wire ret_taken  = (opcode == BRAD_OP_RET);

    wire any_branch = bz_taken | bnz_taken | jmp_taken | call_taken | ret_taken;
    // Branch target: PC + 4 + offset_word*4
    wire [31:0] br_target = dec_pc + 32'd4 + { {16{dec_imm[15]}}, dec_imm };
    wire [31:0] ret_target = dec_rs1_val; // RET reads link register

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ex_valid      <= 1'b0;
            branch_taken  <= 1'b0;
            branch_target <= 32'd0;
            ex_rd         <= 4'd0;
            ex_result     <= 32'd0;
            ex_store_data <= 32'd0;
            ex_addr       <= 32'd0;
            ex_reg_we     <= 1'b0;
            ex_mem_req    <= 1'b0;
            ex_mem_we     <= 1'b0;
        end else if (flush) begin
            ex_valid     <= 1'b0;
            branch_taken <= 1'b0;
        end else if (!stall) begin
            ex_valid   <= dec_valid;
            ex_rd      <= dec_rd;
            ex_reg_we  <= dec_reg_we;

            is_mem   = (opcode == BRAD_OP_LDW) || (opcode == BRAD_OP_STW);
            is_store = (opcode == BRAD_OP_STW);
            is_branch = (opcode >= BRAD_OP_BZ);

            // ALU result
            if (opcode <= BRAD_OP_SHR)
                ex_result <= alu_result;
            else if (opcode == BRAD_OP_ADDI)
                ex_result <= dec_rs1_val + dec_imm;
            else
                ex_result <= 32'd0;

            // Memory address for LDW/STW
            ex_addr       <= dec_rs1_val + dec_imm;
            ex_store_data <= dec_rs2_val;
            ex_mem_req    <= is_mem;
            ex_mem_we     <= is_store;

            // Branch handling
            branch_taken <= any_branch;
            if (ret_taken)
                branch_target <= ret_target;
            else if (any_branch)
                branch_target <= br_target;
            else
                branch_target <= 32'd0;

            // CALL: save return address (PC+4) to link register
            if (opcode == BRAD_OP_CALL && dec_valid) begin
                // Handled by writeback stage
                ex_result <= dec_pc + 32'd4;
                ex_rd     <= BRAD_LR;
                ex_reg_we <= 1'b1;
            end

            // RET: set result to link register value for writeback
            if (opcode == BRAD_OP_RET && dec_valid) begin
                ex_result <= ret_target;
                ex_rd     <= BRAD_R0; // discard
                ex_reg_we <= 1'b0;
            end
        end
    end

endmodule
