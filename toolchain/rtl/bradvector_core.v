`timescale 1ns / 1ps

module bradvector_core (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         enable,
    input  wire [4:0]   warp_id,
    output wire [31:0]  ifetch_addr,
    input  wire [127:0] ifetch_data,
    input  wire         ifetch_valid,
    output wire         ifetch_ready,
    output wire [31:0]  mem_addr,
    output wire [255:0] mem_wdata,
    input  wire [255:0] mem_rdata,
    output wire         mem_write,
    output wire         mem_read,
    input  wire         mem_ready,
    output wire         busy,
    output wire [7:0]   perf_cnt
);

parameter NUM_WARPS = 4;
parameter WARP_SIZE = 32;
parameter VLEN = 256;

localparam PC_WIDTH = 32;
localparam REG_ADDR_W = 5;
localparam NUM_REGS = 32;

reg [4:0] ls_warp;
reg [31:0] pc [0:NUM_WARPS-1];
reg [31:0] active_mask [0:NUM_WARPS-1];
reg [31:0] scoreboard [0:NUM_WARPS-1];
reg [2:0] warp_sel;
reg [127:0] fetch_buf;
reg fetch_buf_valid;
reg [2:0] warp_round;

wire [63:0] slot0_instr, slot1_instr;
wire slot0_valid, slot1_valid;
wire [4:0] slot0_opcode, slot1_opcode;
wire [REG_ADDR_W-1:0] slot0_rd, slot0_rs1, slot0_rs2;
wire [REG_ADDR_W-1:0] slot1_rd, slot1_rs1, slot1_rs2;
wire slot0_is_vec, slot1_is_vec;
wire slot0_is_mem, slot0_is_fpu;
wire slot0_is_sfu;

reg [255:0] regfile [0:NUM_WARPS*NUM_REGS*WARP_SIZE-1];
reg [255:0] alu_out, fpu_out, sfu_out;
reg alu_valid, fpu_valid, sfu_valid;
reg [REG_ADDR_W-1:0] alu_rd, fpu_rd, sfu_rd;
reg [4:0] alu_warp, fpu_warp, sfu_warp;

reg [31:0] ls_addr;
reg [255:0] ls_wdata;
reg ls_write, ls_read;
reg ls_busy;

reg [7:0] cycle_count;

assign busy = |active_mask[0] | |active_mask[1] | |active_mask[2] | |active_mask[3] | ls_busy;
assign perf_cnt = cycle_count;

assign ifetch_addr = pc[warp_sel];
assign ifetch_ready = ~fetch_buf_valid;

assign slot0_instr = fetch_buf[127:64];
assign slot1_instr = fetch_buf[63:0];
assign slot0_valid = fetch_buf_valid;
assign slot1_valid = fetch_buf_valid;

function [4:0] opcode;
    input [63:0] instr;
    begin
        opcode = instr[4:0];
    end
endfunction

function [REG_ADDR_W-1:0] rd_addr;
    input [63:0] instr;
    begin
        rd_addr = instr[9:5];
    end
endfunction

function [REG_ADDR_W-1:0] rs1_addr;
    input [63:0] instr;
    begin
        rs1_addr = instr[14:10];
    end
endfunction

function [REG_ADDR_W-1:0] rs2_addr;
    input [63:0] instr;
    begin
        rs2_addr = instr[19:15];
    end
endfunction

function is_alu_op;
    input [4:0] op;
    begin
        is_alu_op = (op >= 5'd0 && op <= 5'd15);
    end
endfunction

function is_fpu_op;
    input [4:0] op;
    begin
        is_fpu_op = (op >= 5'd16 && op <= 5'd23);
    end
endfunction

function is_sfu_op;
    input [4:0] op;
    begin
        is_sfu_op = (op >= 5'd24 && op <= 5'd27);
    end
endfunction

function is_mem_op;
    input [4:0] op;
    begin
        is_mem_op = (op >= 5'd28 && op <= 5'd31);
    end
endfunction

assign slot0_opcode = opcode(slot0_instr);
assign slot1_opcode = opcode(slot1_instr);
assign slot0_rd = rd_addr(slot0_instr);
assign slot1_rd = rd_addr(slot1_instr);
assign slot0_rs1 = rs1_addr(slot0_instr);
assign slot1_rs1 = rs1_addr(slot1_instr);
assign slot0_rs2 = rs2_addr(slot0_instr);
assign slot1_rs2 = rs2_addr(slot1_instr);
assign slot0_is_vec = is_alu_op(slot0_opcode) || is_fpu_op(slot0_opcode) || is_sfu_op(slot0_opcode) || is_mem_op(slot0_opcode);
assign slot1_is_vec = is_alu_op(slot1_opcode) || is_fpu_op(slot1_opcode) || is_sfu_op(slot1_opcode) || is_mem_op(slot1_opcode);
assign slot0_is_fpu = is_fpu_op(slot0_opcode);
assign slot0_is_sfu = is_sfu_op(slot0_opcode);
assign slot0_is_mem = is_mem_op(slot0_opcode);

reg [4:0] idx;
wire [255:0] rs1_val_0, rs2_val_0;
wire [255:0] rs1_val_1, rs2_val_1;

function [255:0] regfile_read;
    input [4:0] warp;
    input [4:0] reg_addr;
    input [4:0] thread;
    integer addr;
    begin
        addr = warp * NUM_REGS * WARP_SIZE + reg_addr * WARP_SIZE + thread;
        regfile_read = regfile[addr];
    end
endfunction

function sb_check;
    input [31:0] sb;
    input [4:0] r;
    begin
        sb_check = sb[r];
    end
endfunction

reg slot0_issue_ok, slot1_issue_ok;
reg [4:0] issue_warp;
reg [1:0] issue_count;

wire warp0_active = |active_mask[0];
wire warp1_active = |active_mask[1];
wire warp2_active = |active_mask[2];
wire warp3_active = |active_mask[3];

wire [1:0] new_warp_round;

wire [3:0] active_warps = {warp3_active, warp2_active, warp1_active, warp0_active};

always @(*) begin
    slot0_issue_ok = 1'b0;
    slot1_issue_ok = 1'b0;
    issue_count = 2'd0;

    if (fetch_buf_valid && enable) begin
        slot0_issue_ok = 1'b1;
        if (slot0_rd != 0) begin
            if (sb_check(scoreboard[issue_warp], slot0_rd))
                slot0_issue_ok = 1'b0;
        end
        if (slot0_rs1 != 0 && sb_check(scoreboard[issue_warp], slot0_rs1))
            slot0_issue_ok = 1'b0;
        if (slot0_rs2 != 0 && sb_check(scoreboard[issue_warp], slot0_rs2))
            slot0_issue_ok = 1'b0;

        if (slot1_is_vec) begin
            slot1_issue_ok = 1'b1;
            if (slot1_rd != 0) begin
                if (sb_check(scoreboard[issue_warp], slot1_rd))
                    slot1_issue_ok = 1'b0;
            end
            if (slot1_rs1 != 0 && sb_check(scoreboard[issue_warp], slot1_rs1))
                slot1_issue_ok = 1'b0;
            if (slot1_rs2 != 0 && sb_check(scoreboard[issue_warp], slot1_rs2))
                slot1_issue_ok = 1'b0;
        end else begin
            slot1_issue_ok = 1'b1;
        end

        if (slot0_is_mem && slot1_is_mem)
            slot1_issue_ok = 1'b0;
        if (slot0_is_sfu && slot1_is_vec)
            slot1_issue_ok = 1'b0;
    end
end

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        warp_round <= 3'd0;
        warp_sel <= 3'd0;
        fetch_buf_valid <= 1'b0;
        alu_valid <= 1'b0;
        fpu_valid <= 1'b0;
        sfu_valid <= 1'b0;
        ls_busy <= 1'b0;
        cycle_count <= 8'd0;
        for (idx = 0; idx < NUM_WARPS; idx = idx + 1) begin
            pc[idx] <= 32'd0;
            active_mask[idx] <= 32'hFFFFFFFF;
            scoreboard[idx] <= 32'd0;
        end
    end else if (enable) begin
        cycle_count <= cycle_count + 8'd1;

        if (alu_valid) begin
            scoreboard[alu_warp][alu_rd] <= 1'b0;
            alu_valid <= 1'b0;
        end
        if (fpu_valid) begin
            scoreboard[fpu_warp][fpu_rd] <= 1'b0;
            fpu_valid <= 1'b0;
        end
        if (sfu_valid) begin
            scoreboard[sfu_warp][sfu_rd] <= 1'b0;
            sfu_valid <= 1'b0;
        end
        if (ls_busy && mem_ready) begin
            ls_busy <= 1'b0;
            mem_read <= 1'b0;
            mem_write <= 1'b0;
        end

        if (!fetch_buf_valid) begin
            if (ifetch_valid) begin
                fetch_buf <= ifetch_data;
                fetch_buf_valid <= 1'b1;
            end
        end else begin
            issue_warp = warp_sel;

            if (slot0_issue_ok && slot1_issue_ok) begin
                fetch_buf_valid <= 1'b0;
                pc[warp_sel] <= pc[warp_sel] + 32'd16;

                if (slot0_is_mem) begin
                    ls_addr <= regfile_read(warp_sel, slot0_rs1, 0)[31:0];
                    ls_wdata <= regfile_read(warp_sel, slot0_rs2, 0);
                    ls_write <= slot0_opcode[0];
                    ls_read <= ~slot0_opcode[0];
                    ls_busy <= 1'b1;
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                end else if (slot0_is_fpu) begin
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                    fpu_rd <= slot0_rd;
                    fpu_warp <= warp_sel;
                    fpu_valid <= 1'b1;
                end else if (slot0_is_sfu) begin
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                    sfu_rd <= slot0_rd;
                    sfu_warp <= warp_sel;
                    sfu_valid <= 1'b1;
                end else begin
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                    alu_rd <= slot0_rd;
                    alu_warp <= warp_sel;
                    alu_valid <= 1'b1;
                end

                for (idx = 0; idx < WARP_SIZE; idx = idx + 1) begin
                    if (active_mask[warp_sel][idx]) begin
                        if (slot0_is_alu_op(slot0_opcode)) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) +
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end else if (slot0_opcode == 5'd1) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) -
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end else if (slot0_opcode == 5'd2) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) *
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end else if (slot0_opcode == 5'd3) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) &
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end else if (slot0_opcode == 5'd4) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) |
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end else if (slot0_opcode == 5'd5) begin
                            alu_out <= regfile_read(warp_sel, slot0_rs1, idx) ^
                                      regfile_read(warp_sel, slot0_rs2, idx);
                        end
                    end
                end

                if (slot1_is_vec && slot1_issue_ok) begin
                    scoreboard[warp_sel][slot1_rd] <= 1'b1;
                    alu_rd <= slot1_rd;
                    alu_warp <= warp_sel;
                    alu_valid <= 1'b1;

                    for (idx = 0; idx < WARP_SIZE; idx = idx + 1) begin
                        if (active_mask[warp_sel][idx]) begin
                            if (is_alu_op(slot1_opcode)) begin
                            end
                        end
                    end
                end

                warp_round <= warp_round + 3'd1;
                if (warp_round >= 3'd4) warp_round <= 3'd0;

                case (warp_round)
                    3'd0: if (warp1_active) warp_sel <= 3'd1; else if (warp2_active) warp_sel <= 3'd2; else if (warp3_active) warp_sel <= 3'd3; else warp_sel <= 3'd0;
                    3'd1: if (warp2_active) warp_sel <= 3'd2; else if (warp3_active) warp_sel <= 3'd3; else if (warp0_active) warp_sel <= 3'd0; else warp_sel <= 3'd1;
                    3'd2: if (warp3_active) warp_sel <= 3'd3; else if (warp0_active) warp_sel <= 3'd0; else if (warp1_active) warp_sel <= 3'd1; else warp_sel <= 3'd2;
                    3'd3: if (warp0_active) warp_sel <= 3'd0; else if (warp1_active) warp_sel <= 3'd1; else if (warp2_active) warp_sel <= 3'd2; else warp_sel <= 3'd3;
                endcase

            end else if (slot0_issue_ok && !slot1_issue_ok) begin
                fetch_buf_valid <= 1'b0;
                pc[warp_sel] <= pc[warp_sel] + 32'd8;

                if (slot0_is_mem) begin
                    ls_addr <= regfile_read(warp_sel, slot0_rs1, 0)[31:0];
                    ls_wdata <= regfile_read(warp_sel, slot0_rs2, 0);
                    ls_write <= slot0_opcode[0];
                    ls_read <= ~slot0_opcode[0];
                    ls_busy <= 1'b1;
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                end else begin
                    scoreboard[warp_sel][slot0_rd] <= 1'b1;
                    alu_rd <= slot0_rd;
                    alu_warp <= warp_sel;
                    alu_valid <= 1'b1;
                end

                warp_round <= warp_round + 3'd1;
                if (warp_round >= 3'd4) warp_round <= 3'd0;

                case (warp_round)
                    3'd0: if (warp1_active) warp_sel <= 3'd1; else if (warp2_active) warp_sel <= 3'd2; else if (warp3_active) warp_sel <= 3'd3; else warp_sel <= 3'd0;
                    3'd1: if (warp2_active) warp_sel <= 3'd2; else if (warp3_active) warp_sel <= 3'd3; else if (warp0_active) warp_sel <= 3'd0; else warp_sel <= 3'd1;
                    3'd2: if (warp3_active) warp_sel <= 3'd3; else if (warp0_active) warp_sel <= 3'd0; else if (warp1_active) warp_sel <= 3'd1; else warp_sel <= 3'd2;
                    3'd3: if (warp0_active) warp_sel <= 3'd0; else if (warp1_active) warp_sel <= 3'd1; else if (warp2_active) warp_sel <= 3'd2; else warp_sel <= 3'd3;
                endcase
            end
        end

        if (ls_busy && mem_ready) begin
            for (idx = 0; idx < WARP_SIZE; idx = idx + 1) begin
                if (active_mask[ls_warp][idx]) begin
                end
            end
        end
    end
end

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        ls_warp <= 5'd0;
    end else begin
        mem_addr <= ls_addr;
        mem_wdata <= ls_wdata;
        if (ls_write && !ls_busy) begin
            ls_warp <= issue_warp;
        end
    end
end

wire [255:0] writeback_val;
always @(posedge clk) begin
    if (alu_valid) begin
        for (idx = 0; idx < WARP_SIZE; idx = idx + 1) begin
            if (active_mask[alu_warp][idx]) begin
                regfile[alu_warp * NUM_REGS * WARP_SIZE + alu_rd * WARP_SIZE + idx] <= alu_out;
            end
        end
    end
end

endmodule
