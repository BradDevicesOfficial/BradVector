// SPDX-License-Identifier: MIT
// BradISA V1 -- Configurable Branch Predictor
// Supports 3 modes: none (always not-taken), static BTFNT, 2-bit saturating
// Includes direct-mapped BTB with parameterisable size

module brad_bp #(
    parameter BP_TYPE   = 2,        // 0=none, 1=static BTFNT, 2=2-bit saturating
    parameter BTB_SIZE  = 256,      // number of BTB entries (must be power of 2)
    parameter GHIST_BITS = 12       // global history bits (for 2-bit predictor)
) (
    input  wire         clk,
    input  wire         rst_n,

    // Prediction request
    input  wire [31:0]  pred_pc,
    output wire         pred_taken,
    output wire [31:0]  pred_target,

    // Update (from execution)
    input  wire         update_valid,
    input  wire [31:0]  update_pc,
    input  wire         update_taken,
    input  wire [31:0]  update_target,

    // Branch resolution (for 2-bit state machine update)
    input  wire         resolve_mispredict,

    // Flush
    input  wire         flush
);

    // ─── BTB indexing ───────────────────────────────────────────
    localparam BTB_INDEX_BITS = $clog2(BTB_SIZE);
    localparam BTB_TAG_BITS   = 32 - BTB_INDEX_BITS - 2;  // ignore low 2 bits (aligned)

    wire [BTB_INDEX_BITS-1:0] btb_idx  = pred_pc[BTB_INDEX_BITS+1:2];
    wire [BTB_TAG_BITS-1:0]   btb_tag  = pred_pc[31:BTB_INDEX_BITS+2];
    wire [BTB_INDEX_BITS-1:0] upd_btb_idx = update_pc[BTB_INDEX_BITS+1:2];
    wire [BTB_TAG_BITS-1:0]   upd_btb_tag = update_pc[31:BTB_INDEX_BITS+2];

    // ─── BTB storage (direct-mapped) ────────────────────────────
    reg  [BTB_TAG_BITS-1:0] btb_tag_arr  [0:BTB_SIZE-1];
    reg  [31:0]              btb_target   [0:BTB_SIZE-1];
    reg                      btb_valid    [0:BTB_SIZE-1];

    // ─── BTB read (combinatorial) ───────────────────────────────
    wire        btb_hit    = btb_valid[btb_idx] && (btb_tag_arr[btb_idx] == btb_tag);
    wire [31:0] btb_targ   = btb_target[btb_idx];

    // ─── Branch target computation (for BTFNT fallback) ─────────
    // Extract offset field for branch target calculation
    // Since we don't have the instruction, we estimate using the PC + 4 + offset
    // The offset is stored in the BTB; without BTB hit we can't compute target for
    // a purely static predictor. For BTFNT we need the offset from the instruction.
    // In practice, the BTB provides the target when hit; BTFNT only predicts direction.

    // ─── Prediction output ──────────────────────────────────────
    reg  pred_taken_reg;
    reg  [31:0] pred_target_reg;

    // ─── 2-bit saturating counter table (gshare-like) ───────────
    // Indexed by: pred_pc[log2(BTB_SIZE)-1:2] XOR (ghist)
    localparam CNTR_TABLE_SIZE = 256;  // 256 entries for counters
    reg  [1:0] cntr_table [0:CNTR_TABLE_SIZE-1];
    reg  [7:0] upd_cntr_idx;  // index for counter update

    // Global history shift register
    reg  [GHIST_BITS-1:0] ghist;

    wire [7:0] cntr_idx;
    generate
        if (BP_TYPE == 2) begin
            // Gshare: XOR PC index bits with global history
            assign cntr_idx = pred_pc[9:2] ^ ghist[7:0];
        end else begin
            assign cntr_idx = pred_pc[9:2];
        end
    endgenerate

    wire [1:0] cntr_val = cntr_table[cntr_idx];
    // 2-bit saturating counter interpretation:
    // 00 = strongly not taken, 01 = weakly not taken
    // 10 = weakly taken, 11 = strongly taken
    wire        cntr_taken = cntr_val[1];  // MSB is the prediction

    // ─── Prediction logic (combinatorial) ───────────────────────
    always @(*) begin
        case (BP_TYPE)
            0: begin
                // None: always predict not taken
                pred_taken_reg  = 1'b0;
                pred_target_reg = 32'd0;
            end

            1: begin
                // Static BTFNT: predict taken if backward branch
                // We use BTB to provide target; without BTB, predict not-taken
                if (btb_hit) begin
                    // Backward branches have target < PC
                    pred_taken_reg  = ($signed(btb_targ - pred_pc) < 0);
                    pred_target_reg = btb_targ;
                end else begin
                    pred_taken_reg  = 1'b0;
                    pred_target_reg = 32'd0;
                end
            end

            2: begin
                // 2-bit saturating predictor with BTB
                pred_taken_reg  = btb_hit ? cntr_taken : 1'b0;
                pred_target_reg = btb_hit ? btb_targ : 32'd0;
            end

            default: begin
                pred_taken_reg  = 1'b0;
                pred_target_reg = 32'd0;
            end
        endcase
    end

    assign pred_taken  = pred_taken_reg;
    assign pred_target = pred_target_reg;

    // ─── BTB update on branch execution ─────────────────────────
    integer i;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < BTB_SIZE; i = i + 1) begin
                btb_valid[i]   <= 1'b0;
                btb_tag_arr[i] <= {BTB_TAG_BITS{1'b0}};
                btb_target[i]  <= 32'd0;
            end
            ghist <= {GHIST_BITS{1'b0}};
            for (i = 0; i < CNTR_TABLE_SIZE; i = i + 1)
                cntr_table[i] <= 2'b01;  // weakly not-taken by default
        end else if (flush) begin
            // On flush: clear global history (or keep for recovery)
            ghist <= {GHIST_BITS{1'b0}};
        end else if (update_valid) begin
            // Update BTB with actual branch outcome
            btb_tag_arr[upd_btb_idx] <= upd_btb_tag;
            btb_target[upd_btb_idx]  <= update_target;
            btb_valid[upd_btb_idx]   <= 1'b1;

            // Update 2-bit saturating counter if type 2
            if (BP_TYPE == 2) begin
                upd_cntr_idx = update_pc[9:2] ^ ghist[7:0];
                case (cntr_table[upd_cntr_idx])
                    2'b00: cntr_table[upd_cntr_idx] <= update_taken ? 2'b01 : 2'b00;
                    2'b01: cntr_table[upd_cntr_idx] <= update_taken ? 2'b10 : 2'b00;
                    2'b10: cntr_table[upd_cntr_idx] <= update_taken ? 2'b11 : 2'b01;
                    2'b11: cntr_table[upd_cntr_idx] <= update_taken ? 2'b11 : 2'b10;
                    default: cntr_table[upd_cntr_idx] <= 2'b01;
                endcase

                // Update global history
                ghist <= {ghist[GHIST_BITS-2:0], update_taken};
            end
        end
    end

endmodule
