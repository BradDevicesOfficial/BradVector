// SPDX-License-Identifier: MIT
// BradISA V1 -- Register File (synchronous write, async read)

`include "bradisa_defines.v"

module brad_regfile (
    input  wire         clk,
    input  wire         rst_n,
    // Read ports
    input  wire [3:0]   raddr1,
    output reg  [31:0]  rdata1,
    input  wire [3:0]   raddr2,
    output reg  [31:0]  rdata2,
    // Write port
    input  wire         we,
    input  wire [3:0]   waddr,
    input  wire [31:0]  wdata
);

    reg [31:0] regs [0:BRAD_NUM_REGS-1];

    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < BRAD_NUM_REGS; i = i + 1)
                regs[i] <= 32'd0;
        end else if (we && waddr != BRAD_R0) begin
            regs[waddr] <= wdata;
        end
    end

    // Read ports (asynchronous)
    always @(*) begin
        rdata1 = (raddr1 == BRAD_R0) ? 32'd0 : regs[raddr1];
        rdata2 = (raddr2 == BRAD_R0) ? 32'd0 : regs[raddr2];
    end

endmodule
