// SPDX-License-Identifier: MIT
// Bradley ISA V1 -- Verilog RTL Package
// Contains opcode definitions, instruction format constructors,
// pipeline constants, and common types.

`ifndef BRADISA_PKG_SV
`define BRADISA_PKG_SV

// ─── Opcodes ──────────────────────────────────────────────────
localparam BRAD_OP_ADD  = 4'h0;
localparam BRAD_OP_SUB  = 4'h1;
localparam BRAD_OP_MUL  = 4'h2;
localparam BRAD_OP_AND  = 4'h3;
localparam BRAD_OP_OR   = 4'h4;
localparam BRAD_OP_XOR  = 4'h5;
localparam BRAD_OP_SHL  = 4'h6;
localparam BRAD_OP_SHR  = 4'h7;
localparam BRAD_OP_ADDI = 4'h8;
localparam BRAD_OP_LDW  = 4'h9;
localparam BRAD_OP_STW  = 4'hA;
localparam BRAD_OP_BZ   = 4'hB;
localparam BRAD_OP_BNZ  = 4'hC;
localparam BRAD_OP_JMP  = 4'hD;
localparam BRAD_OP_CALL = 4'hE;
localparam BRAD_OP_RET  = 4'hF;

// ─── Register numbers ─────────────────────────────────────────
localparam BRAD_R0  = 4'd0;
localparam BRAD_SP  = 4'd13;
localparam BRAD_LR  = 4'd14;
localparam BRAD_PC  = 4'd15;
localparam BRAD_NUM_REGS = 16;

// ─── Field positions ──────────────────────────────────────────
localparam BRAD_OPCODE_SHIFT = 28;
localparam BRAD_RD_SHIFT     = 24;
localparam BRAD_RS1_SHIFT    = 20;
localparam BRAD_RS2_SHIFT    = 16;

// ─── Pipeline model constants ─────────────────────────────────
localparam BRAD_PHOENIX_DEPTH      = 10;
localparam BRAD_PHOENIX_EXEC_STAGE = 6;
localparam BRAD_FALCON_DEPTH       = 5;
localparam BRAD_FALCON_EXEC_STAGE  = 3;

// ─── Exception vectors ────────────────────────────────────────
localparam BRAD_EXC_RESET      = 32'h00000000;
localparam BRAD_EXC_UNDEF_INSN = 32'h00000004;
localparam BRAD_EXC_PAGE_FAULT = 32'h00000008;
localparam BRAD_EXC_UNALIGNED  = 32'h0000000C;
localparam BRAD_EXC_PRIVILEGE  = 32'h00000010;
localparam BRAD_EXC_SYSCALL    = 32'h00000014;
localparam BRAD_EXC_TIMER      = 32'h00000018;

// ─── MSR indices ──────────────────────────────────────────────
localparam BRAD_MSR_STATUS    = 3'd0;
localparam BRAD_MSR_CAUSE     = 3'd1;
localparam BRAD_MSR_EPC       = 3'd2;
localparam BRAD_MSR_EAR       = 3'd3;
localparam BRAD_MSR_TICK      = 3'd5;

`endif
