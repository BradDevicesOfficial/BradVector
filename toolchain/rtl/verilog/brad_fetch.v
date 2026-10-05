// SPDX-License-Identifier: MIT
// BradISA V1 -- Fetch Stage
// Issues next-PC, reads instruction memory, increments PC.

`include "bradisa_defines.v"

module brad_fetch (
    input  wire         clk,
    input  wire         rst_n,
    // Pipeline control
    input  wire         stall,
    input  wire         flush,
    // PC redirect (from branch resolution)
    input  wire         branch_taken,
    input  wire [31:0]  branch_target,
    // Instruction memory interface
    output reg  [31:0]  imem_addr,
    input  wire [31:0]  imem_rdata,
    // Output to decode stage
    output reg          fetch_valid,
    output reg  [31:0]  fetch_pc,
    output reg  [31:0]  fetch_insn
);

    reg [31:0] pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc          <= 32'd0;
            fetch_valid <= 1'b0;
            fetch_pc    <= 32'd0;
            fetch_insn  <= 32'd0;
            imem_addr   <= 32'd0;
        end else if (flush) begin
            fetch_valid <= 1'b0;
        end else if (!stall) begin
            fetch_valid <= 1'b1;
            fetch_pc    <= pc;
            fetch_insn  <= imem_rdata;
            imem_addr   <= pc;
            if (branch_taken)
                pc <= branch_target;
            else
                pc <= pc + 32'd4;
        end
    end

endmodule
