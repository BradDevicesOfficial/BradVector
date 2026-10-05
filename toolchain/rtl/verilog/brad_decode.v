// SPDX-License-Identifier: MIT
// BradISA V1 -- Decode Stage
// Decodes instruction fields, reads register file, detects hazards.

`include "bradisa_defines.v"

module brad_decode (
    input  wire         clk,
    input  wire         rst_n,
    // From fetch
    input  wire         fetch_valid,
    input  wire [31:0]  fetch_pc,
    input  wire [31:0]  fetch_insn,
    // Pipeline control
    input  wire         stall,
    input  wire         flush,
    // Register file read ports
    output wire [3:0]   rf_raddr1,
    input  wire [31:0]  rf_rdata1,
    output wire [3:0]   rf_raddr2,
    input  wire [31:0]  rf_rdata2,
    // Output to execute stage
    output reg          dec_valid,
    output reg  [3:0]   dec_opcode,
    output reg  [3:0]   dec_rd,
    output reg  [3:0]   dec_rs1,
    output reg  [3:0]   dec_rs2,
    output reg  [31:0]  dec_rs1_val,
    output reg  [31:0]  dec_rs2_val,
    output reg  [31:0]  dec_pc,
    output reg  [31:0]  dec_imm,
    output reg          dec_reg_we
);

    // Decode fields
    wire [3:0] opcode = fetch_insn[BRAD_OPCODE_SHIFT +: 4];
    wire [3:0] rd     = fetch_insn[BRAD_RD_SHIFT     +: 4];
    wire [3:0] rs1    = fetch_insn[BRAD_RS1_SHIFT    +: 4];
    wire [3:0] rs2    = fetch_insn[BRAD_RS2_SHIFT    +: 4];
    wire [15:0] imm16 = fetch_insn[15:0];

    assign rf_raddr1 = rs1;
    assign rf_raddr2 = (opcode == BRAD_OP_STW) ? rs2 : 4'd0;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dec_valid   <= 1'b0;
            dec_opcode  <= 4'd0;
            dec_rd      <= 4'd0;
            dec_rs1     <= 4'd0;
            dec_rs2     <= 4'd0;
            dec_rs1_val <= 32'd0;
            dec_rs2_val <= 32'd0;
            dec_pc      <= 32'd0;
            dec_imm     <= 32'd0;
            dec_reg_we  <= 1'b0;
        end else if (flush) begin
            dec_valid <= 1'b0;
        end else if (!stall) begin
            dec_valid  <= fetch_valid;
            dec_opcode <= opcode;
            dec_rd     <= rd;
            dec_rs1    <= rs1;
            dec_rs2    <= rs2;
            dec_rs1_val <= rf_rdata1;
            dec_rs2_val <= rf_rdata2;
            dec_pc      <= fetch_pc;
            // Sign-extend immediate
            dec_imm     <= { {16{imm16[15]}}, imm16 };
            // Enable writeback for ALU/ADDI/LDW (not branches/stores)
            dec_reg_we  <= (opcode <= BRAD_OP_ADDI) || (opcode == BRAD_OP_LDW);
        end
    end

endmodule
