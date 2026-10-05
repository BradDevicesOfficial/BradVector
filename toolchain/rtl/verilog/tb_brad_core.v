// SPDX-License-Identifier: MIT
// BradISA V1 -- Verilog Testbench for brad_core

`include "bradisa_defines.v"

module tb_brad_core;

    reg         clk;
    reg         rst_n;

    // Instruction memory
    reg  [31:0] imem [0:255];
    wire [31:0] imem_addr;
    wire [31:0] imem_rdata;
    assign imem_rdata = imem[imem_addr[9:2]];

    // Data memory
    reg  [31:0] dmem [0:255];
    wire [31:0] dmem_addr;
    wire        dmem_req;
    wire        dmem_we;
    wire [31:0] dmem_wdata;
    wire [31:0] dmem_rdata;
    assign dmem_rdata = dmem_req ? dmem[dmem_addr[9:2]] : 32'd0;

    integer i;

    // DUT
    brad_core dut (
        .clk(clk), .rst_n(rst_n),
        .imem_addr(imem_addr), .imem_rdata(imem_rdata),
        .dmem_addr(dmem_addr), .dmem_req(dmem_req), .dmem_we(dmem_we),
        .dmem_wdata(dmem_wdata), .dmem_rdata(dmem_rdata)
    );

    // Clock
    always #5 clk = ~clk;

    initial begin
        $dumpfile("tb_brad_core.vcd");
        $dumpvars(0, tb_brad_core);

        // Load program: ADDI r1,r1,100; ADDI r2,r2,0; loop: ADDI r1,r1,-1; ADDI r2,r2,1; BNZ r1,loop; RET
        imem[0] = 32'h81100064;  // ADDI r1, r1, 100
        imem[1] = 32'h82200000;  // ADDI r2, r2, 0
        imem[2] = 32'h8110FFFF;  // ADDI r1, r1, -1   (loop:)
        imem[3] = 32'h82200001;  // ADDI r2, r2, 1
        imem[4] = 32'hC010FFF0;  // BNZ  r1, loop     (offset = -4 words = -16)
        imem[5] = 32'hF0000000;  // RET

        for (i = 6; i < 256; i = i + 1) imem[i] = 32'h00000000;
        for (i = 0; i < 256; i = i + 1) dmem[i] = 32'd0;

        clk = 0;
        rst_n = 0;
        #15 rst_n = 1;

        // Run for 500 cycles
        #5000;

        $display("r1 = %0d  r2 = %0d", dut.regfile.regs[1], dut.regfile.regs[2]);
        if (dut.regfile.regs[1] === 32'd0 && dut.regfile.regs[2] === 32'd100)
            $display("PASS");
        else
            $display("FAIL");

        $finish;
    end

endmodule
