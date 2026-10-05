// SPDX-License-Identifier: MIT
// Kestrel 8-stage dual-issue in-order core
// Stages: F1 -> F2 -> D1 -> D2 -> EX1 -> EX2 -> M -> WB
// Synthesisable Verilog RTL

`include "bradisa_defines.v"

module kestrel_core (
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

    // ─── Branch prediction: BTB (16-entry) + BTFNT ──────────────
    localparam BTB_SIZE = 16;
    reg  [31:0] btb_target [0:BTB_SIZE-1];
    reg         btb_valid [0:BTB_SIZE-1];
    reg  [19:0] btb_tag   [0:BTB_SIZE-1];
    wire [3:0]  btb_idx = pc[5:2];

    // ─── PC / next-PC logic ─────────────────────────────────────
    reg [31:0] pc;
    wire [31:0] pc_plus4 = pc + 32'd4;

    // ─── Fetch buffer (up to 2 instructions) ────────────────────
    reg         fb_has_instr0;
    reg         fb_has_instr1;
    reg [31:0]  fb_instr0;
    reg [31:0]  fb_instr1;
    reg [31:0]  fb_pc0;
    reg [31:0]  fb_pc1;
    wire        fb_full  = fb_has_instr0 && fb_has_instr1;
    wire        fb_empty = !fb_has_instr0;

    // ─── Pipeline registers ─────────────────────────────────────
    // F1 (stage 0) — fetch
    reg         f1_valid;
    reg [31:0]  f1_pc;
    reg [31:0]  f1_instr;

    // D1 (stage 2) — decode slot 0
    reg         d1_valid;
    reg [31:0]  d1_pc;
    reg [3:0]   d1_opcode;
    reg [3:0]   d1_rd;
    reg [3:0]   d1_rs1;
    reg [3:0]   d1_rs2;
    reg [31:0]  d1_rs1_val;
    reg [31:0]  d1_rs2_val;
    reg [31:0]  d1_imm;
    reg         d1_reg_we;

    // D2 (stage 3) — decode slot 1
    reg         d2_valid;
    reg [31:0]  d2_pc;
    reg [3:0]   d2_opcode;
    reg [3:0]   d2_rd;
    reg [3:0]   d2_rs1;
    reg [3:0]   d2_rs2;
    reg [31:0]  d2_rs1_val;
    reg [31:0]  d2_rs2_val;
    reg [31:0]  d2_imm;
    reg         d2_reg_we;

    // EX1 (stage 4) — slot 0 execute
    reg         ex1_valid;
    reg [3:0]   ex1_rd;
    reg [31:0]  ex1_result;
    reg         ex1_reg_we;
    reg         ex1_mem_req;
    reg         ex1_mem_we;
    reg [31:0]  ex1_addr;
    reg [31:0]  ex1_store_data;
    reg [3:0]   ex1_opcode;

    // EX2 (stage 5) — slot 1 execute + memory access
    reg         ex2_valid;
    reg [3:0]   ex2_rd;
    reg [31:0]  ex2_result;
    reg         ex2_reg_we;
    reg         ex2_mem_req;
    reg         ex2_mem_we;
    reg [31:0]  ex2_addr;
    reg [31:0]  ex2_store_data;
    reg [3:0]   ex2_opcode;

    // M0 (stage 6) — slot 0 memory/result ready
    reg         m0_valid;
    reg [3:0]   m0_rd;
    reg [31:0]  m0_result;
    reg         m0_reg_we;

    // M1 (stage 6) — slot 1 memory/result ready
    reg         m1_valid;
    reg [3:0]   m1_rd;
    reg [31:0]  m1_result;
    reg         m1_reg_we;

    // WB (stage 7) — writeback (1 write port, slot 0 priority)
    reg         wb_valid;
    reg [3:0]   wb_rd;
    reg [31:0]  wb_wdata;
    reg         wb_reg_we;

    // ─── Hazard / stall / flush signals ─────────────────────────
    wire        branch_taken;
    wire        pred_mispredict;
    reg         flush_pipeline;
    reg  [31:0] flush_target_pc;

    // Load-to-use: stall decode when D1/D2 reads a reg that EX2 is loading
    // (LDW result is in M after EX2 completes; need to stall 1 cycle)
    wire load_in_ex2 = ex2_valid && ex2_mem_req && !ex2_mem_we && ex2_reg_we;
    wire load_to_use_hazard = d1_valid && load_in_ex2 && (ex2_rd != BRAD_R0) &&
                              ((d1_rs1 == ex2_rd) ||
                               (d1_opcode == BRAD_OP_STW && d1_rs2 == ex2_rd));
    wire load_to_use_hazard_d2 = d2_valid && load_in_ex2 && (ex2_rd != BRAD_R0) &&
                                 ((d2_rs1 == ex2_rd) ||
                                  (d2_opcode == BRAD_OP_STW && d2_rs2 == ex2_rd));
    wire stall_decode = load_to_use_hazard || load_to_use_hazard_d2;

    // ─── Register file ──────────────────────────────────────────
    wire [3:0]  rf_raddr1 = d1_valid ? d1_rs1 : d2_rs1;
    wire [3:0]  rf_raddr2 = d1_valid ? (d1_opcode == BRAD_OP_STW ? d1_rs2 : 4'd0)
                                     : (d2_opcode == BRAD_OP_STW ? d2_rs2 : 4'd0);
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

    // ─── ALU instances ──────────────────────────────────────────
    wire [31:0] alu0_result;
    wire [31:0] alu1_result;

    brad_alu alu0 (
        .a(d1_rs1_val),
        .b(d1_rs2_val),
        .op(d1_opcode),
        .result(alu0_result)
    );

    brad_alu alu1 (
        .a(d2_rs1_val),
        .b(d2_rs2_val),
        .op(d2_opcode),
        .result(alu1_result)
    );

    // ─── Forwarding mux ─────────────────────────────────────────
    // Used by D1/D2 when capturing regfile values:
    // if a later pipeline stage already computed the result, forward it.
    // Priority: EX1 > EX2 > M > regfile
    function [31:0] fwd_val;
        input [3:0]  req_reg;
        input [31:0] rf_val;
        begin
            if (ex1_valid && ex1_reg_we && (ex1_rd == req_reg) && (req_reg != BRAD_R0))
                fwd_val = ex1_result;
            else if (ex2_valid && ex2_reg_we && (ex2_rd == req_reg) && (req_reg != BRAD_R0))
                fwd_val = ex2_result;
            else if (m0_valid && m0_reg_we && (m0_rd == req_reg) && (req_reg != BRAD_R0))
                fwd_val = m0_result;
            else
                fwd_val = rf_val;
        end
    endfunction

    // ─── Fetch (F1) ─────────────────────────────────────────────
    assign imem_addr = pc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc       <= 32'd0;
            f1_valid <= 1'b0;
            f1_pc    <= 32'd0;
            f1_instr <= 32'd0;
        end else if (flush_pipeline) begin
            f1_valid <= 1'b0;
            pc       <= flush_target_pc;
        end else if (!stall_decode && !fb_full) begin
            f1_valid <= 1'b1;
            f1_pc    <= pc;
            f1_instr <= imem_rdata;
            pc       <= pc_plus4;
        end else if (!stall_decode) begin
            f1_valid <= 1'b0;
        end
        // if stalled: hold F1
    end

    // ─── Fetch buffer management ────────────────────────────────
    // D1/D2 consume instructions from the buffer when not stalled.
    // F1 fills the buffer when not stalled and buffer has room.
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fb_has_instr0 <= 1'b0;
            fb_has_instr1 <= 1'b0;
            fb_instr0     <= 32'd0;
            fb_instr1     <= 32'd0;
            fb_pc0        <= 32'd0;
            fb_pc1        <= 32'd0;
        end else if (flush_pipeline) begin
            fb_has_instr0 <= 1'b0;
            fb_has_instr1 <= 1'b0;
        end else begin
            // Consume from buffer (D1/D2 take instructions)
            if (!stall_decode && d1_consume) begin
                fb_has_instr0 <= fb_has_instr1;
                fb_instr0     <= fb_instr1;
                fb_pc0        <= fb_pc1;
                fb_has_instr1 <= 1'b0;
                fb_instr1     <= 32'd0;
                fb_pc1        <= 32'd0;
            end

            // Fill buffer from F1
            if (f1_valid && !stall_decode) begin
                if (!fb_has_instr0) begin
                    fb_has_instr0 <= 1'b1;
                    fb_instr0     <= f1_instr;
                    fb_pc0        <= f1_pc;
                end else if (!fb_has_instr1) begin
                    fb_has_instr1 <= 1'b1;
                    fb_instr1     <= f1_instr;
                    fb_pc1        <= f1_pc;
                end
            end
        end
    end

    // ─── Decode helpers ─────────────────────────────────────────
    function [3:0] get_opcode;
        input [31:0] insn;
        begin get_opcode = insn[BRAD_OPCODE_SHIFT +: 4]; end
    endfunction
    function [3:0] get_rd;
        input [31:0] insn;
        begin get_rd = insn[BRAD_RD_SHIFT +: 4]; end
    endfunction
    function [3:0] get_rs1;
        input [31:0] insn;
        begin get_rs1 = insn[BRAD_RS1_SHIFT +: 4]; end
    endfunction
    function [3:0] get_rs2;
        input [31:0] insn;
        begin get_rs2 = insn[BRAD_RS2_SHIFT +: 4]; end
    endfunction
    function [31:0] get_imm;
        input [31:0] insn;
        reg [15:0] raw;
        begin
            raw = insn[15:0];
            get_imm = { {16{raw[15]}}, raw };
        end
    endfunction
    function is_regwrite_op;
        input [3:0] op;
        begin is_regwrite_op = (op <= BRAD_OP_ADDI) || (op == BRAD_OP_LDW); end
    endfunction

    // ─── D1 stage (slot 0 decode) ───────────────────────────────
    wire d1_consume = fb_has_instr0 && !stall_decode;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d1_valid   <= 1'b0;
            d1_pc      <= 32'd0;
            d1_opcode  <= 4'd0;
            d1_rd      <= 4'd0;
            d1_rs1     <= 4'd0;
            d1_rs2     <= 4'd0;
            d1_rs1_val <= 32'd0;
            d1_rs2_val <= 32'd0;
            d1_imm     <= 32'd0;
            d1_reg_we  <= 1'b0;
        end else if (flush_pipeline) begin
            d1_valid <= 1'b0;
        end else if (d1_consume) begin
            d1_valid   <= 1'b1;
            d1_pc      <= fb_pc0;
            d1_opcode  <= get_opcode(fb_instr0);
            d1_rd      <= get_rd(fb_instr0);
            d1_rs1     <= get_rs1(fb_instr0);
            d1_rs2     <= get_rs2(fb_instr0);
            d1_rs1_val <= fwd_val(get_rs1(fb_instr0), rf_rdata1);
            d1_rs2_val <= fwd_val(get_rs2(fb_instr0), rf_rdata2);
            d1_imm     <= get_imm(fb_instr0);
            d1_reg_we  <= is_regwrite_op(get_opcode(fb_instr0));
        end else if (stall_decode) begin
            // hold D1
        end else begin
            d1_valid <= 1'b0;
        end
    end

    // ─── D2 stage (slot 1 decode) ───────────────────────────────
    // D2 consumes slot 1 only when both slots are available
    wire d2_consume = fb_has_instr0 && fb_has_instr1 && !stall_decode;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d2_valid   <= 1'b0;
            d2_pc      <= 32'd0;
            d2_opcode  <= 4'd0;
            d2_rd      <= 4'd0;
            d2_rs1     <= 4'd0;
            d2_rs2     <= 4'd0;
            d2_rs1_val <= 32'd0;
            d2_rs2_val <= 32'd0;
            d2_imm     <= 32'd0;
            d2_reg_we  <= 1'b0;
        end else if (flush_pipeline) begin
            d2_valid <= 1'b0;
        end else if (d2_consume) begin
            d2_valid   <= 1'b1;
            d2_pc      <= fb_pc1;
            d2_opcode  <= get_opcode(fb_instr1);
            d2_rd      <= get_rd(fb_instr1);
            d2_rs1     <= get_rs1(fb_instr1);
            d2_rs2     <= get_rs2(fb_instr1);
            d2_rs1_val <= fwd_val(get_rs1(fb_instr1), rf_rdata1);
            d2_rs2_val <= fwd_val(get_rs2(fb_instr1), rf_rdata2);
            d2_imm     <= get_imm(fb_instr1);
            d2_reg_we  <= is_regwrite_op(get_opcode(fb_instr1));
        end else if (stall_decode) begin
            // hold D2
        end else begin
            d2_valid <= 1'b0;
        end
    end

    // ─── EX1 stage (slot 0 execute) ─────────────────────────────
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ex1_valid      <= 1'b0;
            ex1_rd         <= 4'd0;
            ex1_result     <= 32'd0;
            ex1_reg_we     <= 1'b0;
            ex1_mem_req    <= 1'b0;
            ex1_mem_we     <= 1'b0;
            ex1_addr       <= 32'd0;
            ex1_store_data <= 32'd0;
            ex1_opcode     <= 4'd0;
        end else if (flush_pipeline) begin
            ex1_valid <= 1'b0;
        end else if (!stall_decode) begin
            ex1_valid  <= d1_valid;
            ex1_rd     <= d1_rd;
            ex1_opcode <= d1_opcode;
            ex1_reg_we <= d1_reg_we;

            // ALU result (register-register ops via brad_alu)
            // ADDI: rs1 + imm
            if (d1_opcode <= BRAD_OP_SHR)
                ex1_result <= alu0_result;
            else if (d1_opcode == BRAD_OP_ADDI)
                ex1_result <= d1_rs1_val + d1_imm;
            else
                ex1_result <= 32'd0;

            // CALL: save PC+4 to LR
            if (d1_opcode == BRAD_OP_CALL) begin
                ex1_result <= d1_pc + 32'd4;
                ex1_rd     <= BRAD_LR;
                ex1_reg_we <= 1'b1;
            end

            // Memory address generation
            ex1_addr       <= d1_rs1_val + d1_imm;
            ex1_store_data <= d1_rs2_val;
            ex1_mem_req    <= (d1_opcode == BRAD_OP_LDW) || (d1_opcode == BRAD_OP_STW);
            ex1_mem_we     <= (d1_opcode == BRAD_OP_STW);
        end
    end

    // ─── EX2 stage (slot 1 execute + memory) ────────────────────
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ex2_valid      <= 1'b0;
            ex2_rd         <= 4'd0;
            ex2_result     <= 32'd0;
            ex2_reg_we     <= 1'b0;
            ex2_mem_req    <= 1'b0;
            ex2_mem_we     <= 1'b0;
            ex2_addr       <= 32'd0;
            ex2_store_data <= 32'd0;
            ex2_opcode     <= 4'd0;
        end else if (flush_pipeline) begin
            ex2_valid <= 1'b0;
        end else if (!stall_decode) begin
            ex2_valid  <= d2_valid;
            ex2_rd     <= d2_rd;
            ex2_opcode <= d2_opcode;
            ex2_reg_we <= d2_reg_we;

            if (d2_opcode <= BRAD_OP_SHR)
                ex2_result <= alu1_result;
            else if (d2_opcode == BRAD_OP_ADDI)
                ex2_result <= d2_rs1_val + d2_imm;
            else
                ex2_result <= 32'd0;

            if (d2_opcode == BRAD_OP_CALL) begin
                ex2_result <= d2_pc + 32'd4;
                ex2_rd     <= BRAD_LR;
                ex2_reg_we <= 1'b1;
            end

            ex2_addr       <= d2_rs1_val + d2_imm;
            ex2_store_data <= d2_rs2_val;
            ex2_mem_req    <= (d2_opcode == BRAD_OP_LDW) || (d2_opcode == BRAD_OP_STW);
            ex2_mem_we     <= (d2_opcode == BRAD_OP_STW);
        end
    end

    // ─── Branch resolution ──────────────────────────────────────
    wire bz_taken   = d1_valid && (d1_opcode == BRAD_OP_BZ)  && (d1_rs1_val == 32'd0);
    wire bnz_taken  = d1_valid && (d1_opcode == BRAD_OP_BNZ) && (d1_rs1_val != 32'd0);
    wire jmp_taken  = d1_valid && (d1_opcode == BRAD_OP_JMP);
    wire call_taken = d1_valid && (d1_opcode == BRAD_OP_CALL);
    wire ret_taken  = d1_valid && (d1_opcode == BRAD_OP_RET);
    assign branch_taken = (bz_taken | bnz_taken | jmp_taken | call_taken | ret_taken);

    wire [31:0] br_target  = d1_pc + 32'd4 + d1_imm;
    wire [31:0] resolved_target = ret_taken ? d1_rs1_val : br_target;

    // ─── Branch prediction ──────────────────────────────────────
    // BTFNT: backward branches (target < PC) are predicted taken
    // BTB: 16-entry direct-mapped, stores targets of executed branches
    wire [19:0] pred_tag   = pc[31:12];
    wire        btb_hit    = btb_valid[btb_idx] && (btb_tag[btb_idx] == pred_tag);
    // Use the instruction being fetched for BTFNT direction
    wire [15:0] raw_pred_imm = imem_rdata[15:0];
    wire [31:0] pred_imm_se = { {16{raw_pred_imm[15]}}, raw_pred_imm };
    // Backward if sign-extended offset is negative (target < PC)
    wire        btfnt_taken = ($signed(pred_imm_se) < 0);
    assign pred_mispredict = branch_taken &&
                             (btb_hit ? (resolved_target != btb_target[btb_idx])
                                      : 1'b1);

    // BTB update
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            integer i;
            for (i = 0; i < BTB_SIZE; i = i + 1) begin
                btb_valid[i]  <= 1'b0;
                btb_target[i] <= 32'd0;
                btb_tag[i]    <= 20'd0;
            end
        end else if (branch_taken) begin
            btb_valid[btb_idx] <= 1'b1;
            btb_target[btb_idx] <= resolved_target;
            btb_tag[btb_idx]    <= pred_tag;
        end
    end

    // ─── Pipeline flush ─────────────────────────────────────────
    always @(*) begin
        if (pred_mispredict) begin
            flush_pipeline  = 1'b1;
            flush_target_pc = resolved_target;
        end else if (branch_taken) begin
            // Branch taken when not predicted (or BTB miss): flush
            flush_pipeline  = 1'b1;
            flush_target_pc = resolved_target;
        end else begin
            flush_pipeline  = 1'b0;
            flush_target_pc = 32'd0;
        end
    end

    // ─── M0 stage (slot 0 memory/result) ───────────────────────
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            m0_valid  <= 1'b0;
            m0_rd     <= 4'd0;
            m0_result <= 32'd0;
            m0_reg_we <= 1'b0;
        end else begin
            m0_valid  <= ex1_valid;
            m0_rd     <= ex1_rd;
            m0_reg_we <= ex1_reg_we;
            if (ex1_valid && ex1_mem_req && !ex1_mem_we)
                m0_result <= dmem_rdata;
            else
                m0_result <= ex1_result;
        end
    end

    // ─── M1 stage (slot 1 memory/result) ───────────────────────
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            m1_valid  <= 1'b0;
            m1_rd     <= 4'd0;
            m1_result <= 32'd0;
            m1_reg_we <= 1'b0;
        end else begin
            m1_valid  <= ex2_valid;
            m1_rd     <= ex2_rd;
            m1_reg_we <= ex2_reg_we;
            if (ex2_valid && ex2_mem_req && !ex2_mem_we)
                m1_result <= dmem_rdata;
            else
                m1_result <= ex2_result;
        end
    end

    // ─── WB stage (writeback, single port) ──────────────────────
    // Slot 0 has priority. Slot 1 writes in the next cycle if slot 0
    // also needs to write. We use a simple arbiter: write m0 first,
    // then m1 if space.
    reg m1_deferred;  // m1 held because m0 wrote this cycle

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wb_valid  <= 1'b0;
            wb_rd     <= 4'd0;
            wb_wdata  <= 32'd0;
            wb_reg_we <= 1'b0;
            m1_deferred <= 1'b0;
        end else begin
            if (m0_valid && m0_reg_we) begin
                // Slot 0 writes this cycle
                wb_valid  <= 1'b1;
                wb_rd     <= m0_rd;
                wb_wdata  <= m0_result;
                wb_reg_we <= m0_reg_we;
                // If slot 1 also wants to write, defer it
                m1_deferred <= (m1_valid && m1_reg_we);
            end else if (m1_valid && m1_reg_we) begin
                // Only slot 1 wants to write
                wb_valid  <= 1'b1;
                wb_rd     <= m1_rd;
                wb_wdata  <= m1_result;
                wb_reg_we <= m1_reg_we;
                m1_deferred <= 1'b0;
            end else begin
                wb_valid  <= 1'b0;
                wb_rd     <= 4'd0;
                wb_wdata  <= 32'd0;
                wb_reg_we <= 1'b0;
                m1_deferred <= 1'b0;
            end
        end
    end

    // ─── Writeback to register file ─────────────────────────────
    assign rf_we    = wb_valid && wb_reg_we && (wb_rd != BRAD_R0);
    assign rf_waddr = wb_rd;
    assign rf_wdata = wb_wdata;

    // ─── Memory port outputs ────────────────────────────────────
    assign dmem_addr  = ex1_addr;
    assign dmem_req   = ex1_mem_req;
    assign dmem_we    = ex1_mem_we;
    assign dmem_wdata = ex1_store_data;

endmodule
