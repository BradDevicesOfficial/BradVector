/// SPDX-License-Identifier: MIT
/// BradGfx — BradVector Compute Engine
/// Dual-issue superscalar wide-vector processor core.
/// 32 threads/warp, 32×32-bit RF/thread, 256-bit vector datapath.
/// Pipeline: Fetch → Decode → Issue → VectorRF → VALU/FPU/SFU → Writeback
/// Round-robin warp scheduler with scoreboard hazard tracking.

module bradvector_core (
    input  wire         clk,
    input  wire         rst_n,
    /// Control
    input  wire         enable,
    input  wire [4:0]   warp_id,
    /// Instruction fetch interface
    output wire [31:0]  ifetch_addr,
    input  wire [127:0] ifetch_data,
    input  wire         ifetch_valid,
    output wire         ifetch_ready,
    /// Memory interface (AXI-lite-like)
    output wire [31:0]  mem_addr,
    output wire [255:0] mem_wdata,
    input  wire [255:0] mem_rdata,
    output wire         mem_write,
    output wire         mem_read,
    input  wire         mem_ready,
    /// Status
    output wire         busy,
    output wire [7:0]   perf_cnt
);

    // =========================================================================
    // Local parameters
    // =========================================================================
    localparam NUM_WARPS      = 4;
    localparam THREADS_PER_WARP = 32;
    localparam REGS_PER_THREAD  = 32;
    localparam VEC_WIDTH       = 256;
    localparam VEC_LANES       = 8;  // 8×FP32
    localparam VEC_LANE_W      = 32;
    localparam SCOREBOARD_DEPTH = 16;
    localparam FETCH_Q_DEPTH   = 4;

    // Opcode groups
    localparam OP_VEC_ALU  = 6'h00;
    localparam OP_FPU_FMA  = 6'h01;
    localparam OP_FPU_ADD  = 6'h02;
    localparam OP_FPU_MUL  = 6'h03;
    localparam OP_FPU_MIN  = 6'h04;
    localparam OP_FPU_MAX  = 6'h05;
    localparam OP_FPU_CMP  = 6'h06;
    localparam OP_FPU_ABS  = 6'h07;
    localparam OP_FPU_NEG  = 6'h08;
    localparam OP_INT_ADD  = 6'h10;
    localparam OP_INT_SUB  = 6'h11;
    localparam OP_INT_MUL  = 6'h12;
    localparam OP_INT64_OP = 6'h13;
    localparam OP_LOGIC    = 6'h14;
    localparam OP_SHIFT    = 6'h15;
    localparam OP_GATHER   = 6'h30;
    localparam OP_SCATTER  = 6'h31;
    localparam OP_LOAD     = 6'h32;
    localparam OP_STORE    = 6'h33;
    localparam OP_BRANCH   = 6'h40;
    localparam OP_SYNC     = 6'h41;

    // =========================================================================
    // Warp state
    // =========================================================================
    reg [31:0]  warp_pc       [0:NUM_WARPS-1];
    reg [4:0]   warp_active   [0:NUM_WARPS-1];
    reg         warp_divergent[0:NUM_WARPS-1];
    reg [4:0]   warp_lane_mask[0:NUM_WARPS-1];
    reg [3:0]   warp_issue_cnt[0:NUM_WARPS-1];

    // =========================================================================
    // Register file: 32 regs × 32-bit per thread, 4 warps
    // =========================================================================
    reg [31:0]  scalar_rf [0:NUM_WARPS*THREADS_PER_WARP*REGS_PER_THREAD-1];
    reg [VEC_WIDTH-1:0] vec_rf [0:NUM_WARPS*THREADS_PER_WARP*REGS_PER_THREAD-1];

    // =========================================================================
    // Scoreboard
    // =========================================================================
    reg [SCOREBOARD_DEPTH-1:0] sb_vec_rdy  [0:NUM_WARPS-1][0:REGS_PER_THREAD-1];
    reg [SCOREBOARD_DEPTH-1:0] sb_scalar_rdy[0:NUM_WARPS-1][0:REGS_PER_THREAD-1];

    // =========================================================================
    // Pipeline stage 1: Fetch (F)
    // =========================================================================
    reg         f_valid;
    reg [31:0]  f_pc;
    reg [127:0] f_insn_packet;  /// 4×32-bit instructions per fetch
    reg [4:0]   f_warp_id;
    reg [1:0]   f_slot;         /// which slot in the packet (0-3)
    reg [3:0]   f_cur_warp;

    // Fetch FSM
    reg [1:0]   fetch_state;
    localparam F_IDLE  = 2'd0;
    localparam F_WAIT  = 2'd1;
    localparam F_READY = 2'd2;

    reg [31:0]  fetch_pc;
    reg [3:0]   fetch_warp_ptr;
    reg [1:0]   fetch_slot_cnt;

    assign ifetch_addr = fetch_pc;
    assign ifetch_ready = (fetch_state == F_READY);

    // Round-robin warp selection for fetch
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            fetch_state    <= F_IDLE;
            fetch_pc       <= 32'd0;
            fetch_warp_ptr <= 4'd0;
            fetch_slot_cnt <= 2'd0;
            f_valid        <= 1'b0;
            f_pc           <= 32'd0;
            f_insn_packet  <= 128'd0;
            f_warp_id      <= 5'd0;
            f_slot         <= 2'd0;
            f_cur_warp     <= 4'd0;
        end else if (!enable) begin
            fetch_state <= F_IDLE;
            f_valid     <= 1'b0;
        end else begin
            f_valid <= 1'b0;
            case (fetch_state)
                F_IDLE: begin
                    // Select next warp round-robin
                    fetch_warp_ptr <= (fetch_warp_ptr + 1) & (NUM_WARPS-1);
                    fetch_pc       <= warp_pc[fetch_warp_ptr];
                    f_cur_warp     <= fetch_warp_ptr;
                    if (warp_active[fetch_warp_ptr] != 5'd0)
                        fetch_state <= F_WAIT;
                end

                F_WAIT: begin
                    if (ifetch_valid) begin
                        f_insn_packet <= ifetch_data;
                        f_pc          <= fetch_pc;
                        f_warp_id     <= {1'b0, f_cur_warp};
                        f_slot        <= 2'd0;
                        fetch_slot_cnt <= 2'd0;
                        fetch_state   <= F_READY;
                    end
                end

                F_READY: begin
                    // Issue one instruction per cycle from the packet
                    f_valid            <= 1'b1;
                    f_pc               <= fetch_pc + {fetch_slot_cnt, 4'd0};
                    f_warp_id          <= {1'b0, f_cur_warp};
                    f_slot             <= fetch_slot_cnt;
                    f_insn_packet      <= ifetch_data;
                    if (fetch_slot_cnt == 2'd3 || !ifetch_valid) begin
                        fetch_state    <= F_IDLE;
                    end else begin
                        fetch_slot_cnt <= fetch_slot_cnt + 2'd1;
                    end
                end
            endcase
        end
    end

    // =========================================================================
    // Pipeline stage 2: Decode (D)
    // =========================================================================
    reg         d_valid;
    reg [31:0]  d_pc;
    reg [5:0]   d_opcode;
    reg [4:0]   d_dst_reg;
    reg [4:0]   d_src1_reg;
    reg [4:0]   d_src2_reg;
    reg [4:0]   d_src3_reg;
    reg [31:0]  d_imm;
    reg         d_is_vector;
    reg         d_is_scalar;
    reg         d_is_mem;
    reg         d_is_alu;
    reg         d_is_fpu;
    reg         d_is_branch;
    reg [1:0]   d_width;       /// 0=32b, 1=64b, 2=128b, 3=256b
    reg [4:0]   d_warp_id;
    reg         d_predicate_on;
    reg [4:0]   d_pred_reg;
    reg [1:0]   d_slot;

    // Decode the instruction
    wire [5:0]  raw_op    = f_insn_packet[{f_slot, 5'd5} +: 6];
    wire [4:0]  raw_dst   = f_insn_packet[{f_slot, 5'd5} + 6 +: 5];
    wire [4:0]  raw_src1  = f_insn_packet[{f_slot, 5'd5} + 11 +: 5];
    wire [4:0]  raw_src2  = f_insn_packet[{f_slot, 5'd5} + 16 +: 5];
    wire [4:0]  raw_src3  = f_insn_packet[{f_slot, 5'd5} + 21 +: 5];
    wire [4:0]  raw_pred  = f_insn_packet[{f_slot, 5'd5} + 26 +: 5];
    wire        raw_pred_en = f_insn_packet[{f_slot, 5'd5} + 31];

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            d_valid       <= 1'b0;
            d_pc          <= 32'd0;
            d_opcode      <= 6'd0;
            d_dst_reg     <= 5'd0;
            d_src1_reg    <= 5'd0;
            d_src2_reg    <= 5'd0;
            d_src3_reg    <= 5'd0;
            d_imm         <= 32'd0;
            d_is_vector   <= 1'b0;
            d_is_scalar   <= 1'b0;
            d_is_mem      <= 1'b0;
            d_is_alu      <= 1'b0;
            d_is_fpu      <= 1'b0;
            d_is_branch   <= 1'b0;
            d_width       <= 2'd0;
            d_warp_id     <= 5'd0;
            d_predicate_on <= 1'b0;
            d_pred_reg    <= 5'd0;
            d_slot        <= 2'd0;
        end else begin
            d_valid       <= f_valid;
            d_pc          <= f_pc;
            d_opcode      <= raw_op;
            d_dst_reg     <= raw_dst;
            d_src1_reg    <= raw_src1;
            d_src2_reg    <= raw_src2;
            d_src3_reg    <= raw_src3;
            d_imm         <= f_insn_packet[{f_slot, 5'd5} + 32 +: 32];
            d_predicate_on <= raw_pred_en;
            d_pred_reg    <= raw_pred;
            d_warp_id     <= f_warp_id;
            d_slot        <= f_slot;
            d_is_vector   <= (raw_op[5:4] == 2'b00);
            d_is_scalar   <= (raw_op[5:4] == 2'b01 && raw_op[3:2] != 2'b11);
            d_is_mem      <= (raw_op[5:4] == 2'b11);
            d_is_alu      <= (raw_op[5:4] == 2'b00 || raw_op[5:4] == 2'b01);
            d_is_fpu      <= (raw_op[5:4] == 2'b00);
            d_is_branch   <= (raw_op[5:4] == 2'b10);
            d_width       <= f_insn_packet[{f_slot, 5'd5} + 37 +: 2];
        end
    end

    // =========================================================================
    // Pipeline stage 3: Issue (I)
    // =========================================================================
    reg         i_valid;
    reg [31:0]  i_pc;
    reg [5:0]   i_opcode;
    reg [4:0]   i_dst_reg;
    reg [4:0]   i_src1_reg;
    reg [4:0]   i_src2_reg;
    reg [4:0]   i_src3_reg;
    reg [31:0]  i_imm;
    reg [31:0]  i_src1_val;
    reg [31:0]  i_src2_val;
    reg [31:0]  i_src3_val;
    reg         i_is_vector;
    reg         i_is_scalar;
    reg         i_is_mem;
    reg         i_is_alu;
    reg         i_is_fpu;
    reg         i_is_branch;
    reg [1:0]   i_width;
    reg [4:0]   i_warp_id;
    reg         i_pred_ok;

    // Issue stall detection via scoreboard
    wire        sb_hazard_vec  = sb_vec_rdy[i_warp_id][i_src1_reg] != 0 ||
                                 sb_vec_rdy[i_warp_id][i_src2_reg] != 0;
    wire        sb_hazard_scalar = sb_scalar_rdy[i_warp_id][i_src1_reg] != 0 ||
                                   sb_scalar_rdy[i_warp_id][i_src2_reg] != 0;
    wire        i_hazard = (i_is_vector && sb_hazard_vec) ||
                           (i_is_scalar && sb_hazard_scalar);
    reg         i_stall;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            i_valid     <= 1'b0;
            i_pc        <= 32'd0;
            i_opcode    <= 6'd0;
            i_dst_reg   <= 5'd0;
            i_src1_reg  <= 5'd0;
            i_src2_reg  <= 5'd0;
            i_src3_reg  <= 5'd0;
            i_imm       <= 32'd0;
            i_src1_val  <= 32'd0;
            i_src2_val  <= 32'd0;
            i_src3_val  <= 32'd0;
            i_is_vector <= 1'b0;
            i_is_scalar <= 1'b0;
            i_is_mem    <= 1'b0;
            i_is_alu    <= 1'b0;
            i_is_fpu    <= 1'b0;
            i_is_branch <= 1'b0;
            i_width     <= 2'd0;
            i_warp_id   <= 5'd0;
            i_pred_ok   <= 1'b1;
            i_stall     <= 1'b0;
        end else begin
            // Hazard check: stall decode->issue if register not ready
            if (d_valid && !i_stall) begin
                i_stall <= (d_is_vector && (|sb_vec_rdy[d_warp_id][d_src1_reg])) ||
                           (d_is_scalar && (|sb_scalar_rdy[d_warp_id][d_src1_reg])) ||
                           (d_is_scalar && d_src2_reg != 0 &&
                            (|sb_scalar_rdy[d_warp_id][d_src2_reg]));
            end else if (i_stall) begin
                // Check if hazard cleared
                i_stall <= (i_is_vector && (|sb_vec_rdy[i_warp_id][i_src1_reg])) ||
                           (i_is_scalar && (|sb_scalar_rdy[i_warp_id][i_src1_reg]));
            end

            if (i_stall) begin
                // Hold issue stage during stall
                i_valid <= 1'b0;
            end else if (d_valid) begin
                i_valid     <= 1'b1;
                i_pc        <= d_pc;
                i_opcode    <= d_opcode;
                i_dst_reg   <= d_dst_reg;
                i_src1_reg  <= d_src1_reg;
                i_src2_reg  <= d_src2_reg;
                i_src3_reg  <= d_src3_reg;
                i_imm       <= d_imm;
                i_is_vector <= d_is_vector;
                i_is_scalar <= d_is_scalar;
                i_is_mem    <= d_is_mem;
                i_is_alu    <= d_is_alu;
                i_is_fpu    <= d_is_fpu;
                i_is_branch <= d_is_branch;
                i_width     <= d_width;
                i_warp_id   <= d_warp_id;
                i_pred_ok   <= !d_predicate_on ||
                               (scalar_rf[d_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + d_pred_reg] != 0);

                // Read scalar register file
                i_src1_val <= scalar_rf[d_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + d_src1_reg];
                i_src2_val <= scalar_rf[d_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + d_src2_reg];
                i_src3_val <= scalar_rf[d_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + d_src3_reg];
            end else begin
                i_valid <= 1'b0;
            end
        end
    end

    // =========================================================================
    // Pipeline stage 4: Vector Register Read / Scalar Bypass (R)
    // =========================================================================
    reg         r_valid;
    reg [31:0]  r_pc;
    reg [5:0]   r_opcode;
    reg [4:0]   r_dst_reg;
    reg [31:0]  r_src1_val;
    reg [31:0]  r_src2_val;
    reg [31:0]  r_src3_val;
    reg [31:0]  r_imm;
    reg [VEC_WIDTH-1:0] r_vec_src1;
    reg [VEC_WIDTH-1:0] r_vec_src2;
    reg [VEC_WIDTH-1:0] r_vec_src3;
    reg [255:0] r_gather_data;
    reg         r_is_vector;
    reg         r_is_scalar;
    reg         r_is_mem;
    reg         r_is_alu;
    reg         r_is_fpu;
    reg [1:0]   r_width;
    reg [4:0]   r_warp_id;
    reg         r_pred_ok;

    // Lane-active mask for vector ops (handles divergence)
    reg [VEC_LANES-1:0] r_lane_mask;
    integer lane_idx_r;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            r_valid      <= 1'b0;
            r_pc         <= 32'd0;
            r_opcode     <= 6'd0;
            r_dst_reg    <= 5'd0;
            r_src1_val   <= 32'd0;
            r_src2_val   <= 32'd0;
            r_src3_val   <= 32'd0;
            r_imm        <= 32'd0;
            r_vec_src1   <= 256'd0;
            r_vec_src2   <= 256'd0;
            r_vec_src3   <= 256'd0;
            r_is_vector  <= 1'b0;
            r_is_scalar  <= 1'b0;
            r_is_mem     <= 1'b0;
            r_is_alu     <= 1'b0;
            r_is_fpu     <= 1'b0;
            r_width      <= 2'd0;
            r_warp_id    <= 5'd0;
            r_pred_ok    <= 1'b1;
            r_lane_mask  <= {VEC_LANES{1'b1}};
            r_gather_data <= 256'd0;
        end else if (i_valid && i_pred_ok) begin
            r_valid     <= 1'b1;
            r_pc        <= i_pc;
            r_opcode    <= i_opcode;
            r_dst_reg   <= i_dst_reg;
            r_src1_val  <= i_src1_val;
            r_src2_val  <= i_src2_val;
            r_src3_val  <= i_src3_val;
            r_imm       <= i_imm;
            r_is_vector <= i_is_vector;
            r_is_scalar <= i_is_scalar;
            r_is_mem    <= i_is_mem;
            r_is_alu    <= i_is_alu;
            r_is_fpu    <= i_is_fpu;
            r_width     <= i_width;
            r_warp_id   <= i_warp_id;
            r_pred_ok   <= i_pred_ok;
            r_lane_mask <= warp_lane_mask[i_warp_id];

            // Vector register file read (vector ops read 256-bit width)
            r_vec_src1 <= vec_rf[i_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + i_src1_reg];
            r_vec_src2 <= vec_rf[i_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + i_src2_reg];
            r_vec_src3 <= vec_rf[i_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + i_src3_reg];

            // Mark destination busy in scoreboard
            if (i_is_vector && i_dst_reg != 0) begin
                sb_vec_rdy[i_warp_id][i_dst_reg] <= {SCOREBOARD_DEPTH{1'b1}};
            end
            if (i_is_scalar && i_dst_reg != 0) begin
                sb_scalar_rdy[i_warp_id][i_dst_reg] <= {SCOREBOARD_DEPTH{1'b1}};
            end

            // Gather: indexed load into gather buffer
            if (i_opcode == OP_GATHER) begin
                r_gather_data <= mem_rdata;
            end
        end else if (i_valid && !i_pred_ok) begin
            // Predicate false: skip instruction
            r_valid <= 1'b0;
        end else begin
            r_valid <= 1'b0;
        end
    end

    // =========================================================================
    // Pipeline stage 5: Execute (E) — VALU, FPU, SFU
    // =========================================================================
    reg         e_valid;
    reg [31:0]  e_pc;
    reg [5:0]   e_opcode;
    reg [4:0]   e_dst_reg;
    reg [31:0]  e_scalar_result;
    reg [VEC_WIDTH-1:0] e_vec_result;
    reg [255:0] e_mem_wdata;
    reg [31:0]  e_mem_addr;
    reg         e_mem_write;
    reg         e_mem_read;
    reg         e_branch_taken;
    reg [31:0]  e_branch_target;
    reg [4:0]   e_warp_id;
    reg         e_is_vector;
    reg         e_is_scalar;
    reg         e_is_mem;
    reg [1:0]   e_width;
    reg [VEC_LANES-1:0] e_lane_mask;

    // Lane-wise vector ALU
    wire [31:0] vec_lane_in1 [0:VEC_LANES-1];
    wire [31:0] vec_lane_in2 [0:VEC_LANES-1];
    wire [31:0] vec_lane_in3 [0:VEC_LANES-1];
    reg  [31:0] vec_lane_out [0:VEC_LANES-1];
    reg  [31:0] vec_lane_fma [0:VEC_LANES-1];

    genvar gl;
    generate
        for (gl = 0; gl < VEC_LANES; gl = gl + 1) begin
            assign vec_lane_in1[gl] = r_vec_src1[gl*32 +: 32];
            assign vec_lane_in2[gl] = r_vec_src2[gl*32 +: 32];
            assign vec_lane_in3[gl] = r_vec_src3[gl*32 +: 32];
        end
    endgenerate

    integer li;
    always @(*) begin
        e_scalar_result = 32'd0;
        e_vec_result    = 256'd0;
        e_mem_addr      = 32'd0;
        e_mem_wdata     = 256'd0;
        e_mem_write     = 1'b0;
        e_mem_read      = 1'b0;
        e_branch_taken  = 1'b0;
        e_branch_target = 32'd0;

        for (li = 0; li < VEC_LANES; li = li + 1) begin
            vec_lane_fma[li] = 32'd0;
        end

        if (r_valid && r_pred_ok) begin
            case (r_opcode)
                // ── Vector FPU operations ──
                OP_FPU_FMA: begin
                    for (li = 0; li < VEC_LANES; li = li + 1) begin
                        if (r_lane_mask[li])
                            vec_lane_fma[li] = $fma(vec_lane_in1[li], vec_lane_in2[li], vec_lane_in3[li]);
                        else
                            vec_lane_fma[li] = vec_lane_in1[li];
                    end
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = vec_lane_fma[li];
                end

                OP_FPU_ADD: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            ($unsigned(vec_lane_in1[li]) + $unsigned(vec_lane_in2[li])) :
                            vec_lane_in1[li];
                end

                OP_FPU_MUL: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            ($unsigned(vec_lane_in1[li]) * $unsigned(vec_lane_in2[li])) :
                            vec_lane_in1[li];
                end

                OP_FPU_MIN: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (($signed(vec_lane_in1[li]) < $signed(vec_lane_in2[li])) ?
                                vec_lane_in1[li] : vec_lane_in2[li]) :
                            vec_lane_in1[li];
                end

                OP_FPU_MAX: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (($signed(vec_lane_in1[li]) > $signed(vec_lane_in2[li])) ?
                                vec_lane_in1[li] : vec_lane_in2[li]) :
                            vec_lane_in1[li];
                end

                OP_FPU_CMP: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (vec_lane_in1[li] == vec_lane_in2[li] ? 32'd1 : 32'd0) :
                            vec_lane_in1[li];
                end

                OP_FPU_ABS: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (vec_lane_in1[li][31] ? (~vec_lane_in1[li] + 1) : vec_lane_in1[li]) :
                            vec_lane_in1[li];
                end

                OP_FPU_NEG: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (~vec_lane_in1[li] + 1) : vec_lane_in1[li];
                end

                // ── Integer ALU operations ──
                OP_INT_ADD: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (vec_lane_in1[li] + vec_lane_in2[li]) : vec_lane_in1[li];
                    e_scalar_result = r_src1_val + r_src2_val;
                end

                OP_INT_SUB: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (vec_lane_in1[li] - vec_lane_in2[li]) : vec_lane_in1[li];
                    e_scalar_result = r_src1_val - r_src2_val;
                end

                OP_INT_MUL: begin
                    for (li = 0; li < VEC_LANES; li = li + 1)
                        e_vec_result[li*32 +: 32] = r_lane_mask[li] ?
                            (vec_lane_in1[li] * vec_lane_in2[li]) : vec_lane_in1[li];
                    e_scalar_result = r_src1_val * r_src2_val;
                end

                OP_INT64_OP: begin
                    // 64-bit split across lanes 0,1 → low,high
                    if (r_lane_mask[0])
                        e_vec_result[63:0] = {vec_lane_in1[1], vec_lane_in1[0]} +
                                             {vec_lane_in2[1], vec_lane_in2[0]};
                    else
                        e_vec_result[63:0] = {vec_lane_in1[1], vec_lane_in1[0]};
                end

                OP_LOGIC: begin
                    case (r_imm[3:0])
                        4'd0: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = vec_lane_in1[li] & vec_lane_in2[li];
                        4'd1: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = vec_lane_in1[li] | vec_lane_in2[li];
                        4'd2: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = vec_lane_in1[li] ^ vec_lane_in2[li];
                        4'd3: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = ~vec_lane_in1[li];
                        default: e_vec_result = r_vec_src1;
                    endcase
                end

                OP_SHIFT: begin
                    case (r_imm[1:0])
                        2'd0: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = vec_lane_in1[li] << vec_lane_in2[li][4:0];
                        2'd1: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = vec_lane_in1[li] >> vec_lane_in2[li][4:0];
                        2'd2: for (li = 0; li < VEC_LANES; li = li + 1)
                                e_vec_result[li*32 +: 32] = $signed(vec_lane_in1[li]) >>> vec_lane_in2[li][4:0];
                        default: e_vec_result = r_vec_src1;
                    endcase
                end

                // ── Memory operations ──
                OP_GATHER: begin
                    // Gather from previously loaded data
                    e_vec_result = r_gather_data;
                end

                OP_SCATTER: begin
                    e_mem_addr  = r_src1_val + r_src2_val;
                    e_mem_wdata = r_vec_src1;
                    e_mem_write = 1'b1;
                end

                OP_LOAD: begin
                    e_mem_addr = r_src1_val + r_imm;
                    e_mem_read = 1'b1;
                end

                OP_STORE: begin
                    e_mem_addr  = r_src1_val + r_imm;
                    e_mem_wdata = r_vec_src1;
                    e_mem_write = 1'b1;
                end

                // ── Branch operations ──
                OP_BRANCH: begin
                    e_branch_taken = 1'b1;
                    e_branch_target = r_src1_val + r_imm;
                end

                OP_SYNC: begin
                    // Barrier synchronization (stub)
                    e_scalar_result = 32'd0;
                end

                default: begin
                    e_scalar_result = r_src1_val;
                    e_vec_result    = r_vec_src1;
                end
            endcase
        end
    end

    // Execute pipeline register
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            e_valid        <= 1'b0;
            e_pc           <= 32'd0;
            e_opcode       <= 6'd0;
            e_dst_reg      <= 5'd0;
            e_scalar_result <= 32'd0;
            e_vec_result   <= 256'd0;
            e_mem_wdata    <= 256'd0;
            e_mem_addr     <= 32'd0;
            e_mem_write    <= 1'b0;
            e_mem_read     <= 1'b0;
            e_branch_taken <= 1'b0;
            e_branch_target <= 32'd0;
            e_warp_id      <= 5'd0;
            e_is_vector    <= 1'b0;
            e_is_scalar    <= 1'b0;
            e_is_mem       <= 1'b0;
            e_width        <= 2'd0;
            e_lane_mask    <= 0;
        end else begin
            e_valid        <= r_valid && r_pred_ok;
            e_pc           <= r_pc;
            e_opcode       <= r_opcode;
            e_dst_reg      <= r_dst_reg;
            e_scalar_result <= e_scalar_result;
            e_vec_result   <= e_vec_result;
            e_mem_addr     <= e_mem_addr;
            e_mem_write    <= e_mem_write;
            e_mem_read     <= e_mem_read;
            e_branch_taken <= e_branch_taken;
            e_branch_target <= e_branch_target;
            e_warp_id      <= r_warp_id;
            e_is_vector    <= r_is_vector;
            e_is_scalar    <= r_is_scalar;
            e_is_mem       <= r_is_mem;
            e_width        <= r_width;
            e_lane_mask    <= r_lane_mask;
        end
    end

    // Memory interface combinational output
    assign mem_addr   = e_mem_addr;
    assign mem_wdata  = e_mem_wdata;
    assign mem_write  = e_mem_write;
    assign mem_read   = e_mem_read;

    // =========================================================================
    // Pipeline stage 6: Writeback (W)
    // =========================================================================
    reg         w_valid;
    reg [4:0]   w_dst_reg;
    reg [31:0]  w_scalar_result;
    reg [VEC_WIDTH-1:0] w_vec_result;
    reg         w_is_vector;
    reg         w_is_scalar;
    reg         w_is_mem;
    reg [4:0]   w_warp_id;
    reg [255:0] w_mem_rdata;
    reg [31:0]  w_mem_addr_reg;

    // Scoreboard decrement for writeback clear
    integer sbi;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            w_valid        <= 1'b0;
            w_dst_reg      <= 5'd0;
            w_scalar_result <= 32'd0;
            w_vec_result   <= 256'd0;
            w_is_vector    <= 1'b0;
            w_is_scalar    <= 1'b0;
            w_is_mem       <= 1'b0;
            w_warp_id      <= 5'd0;
            w_mem_rdata    <= 256'd0;
            w_mem_addr_reg <= 32'd0;
        end else begin
            w_valid        <= e_valid;
            w_dst_reg      <= e_dst_reg;
            w_scalar_result <= e_scalar_result;
            w_is_vector    <= e_is_vector;
            w_is_scalar    <= e_is_scalar;
            w_is_mem       <= e_is_mem;
            w_warp_id      <= e_warp_id;
            w_mem_rdata    <= mem_rdata;
            w_mem_addr_reg <= e_mem_addr;

            // For memory loads, result comes from mem_rdata
            if (e_is_mem && e_mem_read) begin
                w_vec_result <= mem_rdata;
            end else begin
                w_vec_result <= e_vec_result;
            end
        end
    end

    // =========================================================================
    // Register file writeback and scoreboard release
    // =========================================================================
    integer wi, wj;
    reg [SCOREBOARD_DEPTH-1:0] sb_mask_next;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (wi = 0; wi < NUM_WARPS; wi = wi + 1) begin
                for (wj = 0; wj < REGS_PER_THREAD; wj = wj + 1) begin
                    sb_vec_rdy[wi][wj]   <= 0;
                    sb_scalar_rdy[wi][wj] <= 0;
                end
            end
        end else begin
            // Release scoreboard bits for completed operations
            if (w_valid) begin
                if (w_is_vector && w_dst_reg != 0) begin
                    sb_mask_next = sb_vec_rdy[w_warp_id][w_dst_reg];
                    sb_vec_rdy[w_warp_id][w_dst_reg] <= sb_mask_next >> 1;
                end
                if (w_is_scalar && w_dst_reg != 0) begin
                    sb_mask_next = sb_scalar_rdy[w_warp_id][w_dst_reg];
                    sb_scalar_rdy[w_warp_id][w_dst_reg] <= sb_mask_next >> 1;
                end
            end

            // Writeback scalar register file
            if (w_valid && w_dst_reg != 0) begin
                if (w_is_scalar) begin
                    scalar_rf[w_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + w_dst_reg] <= w_scalar_result;
                end
            end
        end
    end

    // Vector register file write (separate process)
    always @(posedge clk) begin
        if (w_valid && w_dst_reg != 0 && (w_is_vector || (w_is_mem && mem_read))) begin
            vec_rf[w_warp_id*THREADS_PER_WARP*REGS_PER_THREAD + w_dst_reg] <= w_vec_result;
        end
    end

    // =========================================================================
    // Warp scheduler state update
    // =========================================================================
    integer wsi;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (wsi = 0; wsi < NUM_WARPS; wsi = wsi + 1) begin
                warp_pc[wsi]        <= 32'd0;
                warp_active[wsi]    <= 5'd0;
                warp_divergent[wsi] <= 1'b0;
                warp_lane_mask[wsi] <= {THREADS_PER_WARP{1'b1}};
                warp_issue_cnt[wsi] <= 4'd0;
            end
        end else if (enable) begin
            // Update PC on branches
            if (e_valid && e_branch_taken) begin
                warp_pc[e_warp_id] <= e_branch_target;
            end else begin
                // Advance PC when instruction issued
                warp_pc[e_warp_id] <= warp_pc[e_warp_id] + 32'd4;
            end

            // Track issued instruction count
            if (e_valid) begin
                warp_issue_cnt[e_warp_id] <= warp_issue_cnt[e_warp_id] + 1;
            end

            // Lane mask management (divergence)
            if (e_valid && e_opcode == OP_BRANCH) begin
                warp_divergent[e_warp_id] <= 1'b1;
            end
        end
    end

    // =========================================================================
    // Performance counter
    // =========================================================================
    reg [7:0]  perf_cnt_r;
    reg [31:0] cycle_cnt;
    reg [31:0] instr_cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            perf_cnt_r <= 8'd0;
            cycle_cnt  <= 32'd0;
            instr_cnt  <= 32'd0;
        end else if (enable) begin
            cycle_cnt <= cycle_cnt + 1;
            if (w_valid) begin
                instr_cnt <= instr_cnt + 1;
            end
            // Performance: instructions per cycle * 10
            perf_cnt_r <= (instr_cnt * 8'd10) / (cycle_cnt + 1);
        end
    end

    assign perf_cnt = perf_cnt_r;
    assign busy = (fetch_state != F_IDLE) || f_valid || d_valid || i_valid ||
                  r_valid || e_valid || w_valid;

endmodule
