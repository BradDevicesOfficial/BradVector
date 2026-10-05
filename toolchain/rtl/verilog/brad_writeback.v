// SPDX-License-Identifier: MIT
// BradISA V1 -- Writeback Stage
// Writes ALU result or load data back to register file.

`include "bradisa_defines.v"

module brad_writeback (
    input  wire         clk,
    input  wire         rst_n,
    // From execute
    input  wire         ex_valid,
    input  wire [3:0]   ex_rd,
    input  wire [31:0]  ex_result,
    input  wire [31:0]  ex_addr,
    input  wire         ex_reg_we,
    input  wire         ex_mem_req,
    input  wire         ex_mem_we,
    input  wire [31:0]  ex_store_data,
    // Data memory
    input  wire [31:0]  dmem_rdata,
    output wire [31:0]  dmem_addr,
    output wire         dmem_req,
    output wire         dmem_we,
    output wire [31:0]  dmem_wdata,
    // Pipeline control
    input  wire         stall,
    input  wire         flush,
    // Register file write port
    output reg          rf_we,
    output reg  [3:0]   rf_waddr,
    output reg  [31:0]  rf_wdata
);

    assign dmem_addr  = ex_addr;
    assign dmem_req   = ex_mem_req;
    assign dmem_we    = ex_mem_we;
    assign dmem_wdata = ex_store_data;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rf_we    <= 1'b0;
            rf_waddr <= 4'd0;
            rf_wdata <= 32'd0;
        end else if (flush) begin
            rf_we <= 1'b0;
        end else if (!stall) begin
            rf_we    <= ex_valid && ex_reg_we && (ex_rd != BRAD_R0);
            rf_waddr <= ex_rd;
            if (ex_mem_req && !ex_mem_we) begin
                // Load: use memory read data
                rf_wdata <= dmem_rdata;
            end else begin
                rf_wdata <= ex_result;
            end
        end
    end

endmodule
