`timescale 1ns / 1ps

module neurostream_core (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         enable,
    input  wire [1:0]   quality_mode,
    output wire [15:0]  weight_addr,
    input  wire [127:0] weight_rdata,
    output wire         weight_req,
    input  wire         weight_valid,
    input  wire [31:0]  frame_pixel,
    input  wire         frame_valid,
    output wire [31:0]  out_pixel,
    output wire         out_valid,
    input  wire         out_ready,
    output wire         busy
);

localparam SRAM_SIZE = 65536;
localparam SRAM_ADDR_W = 16;
localparam L1_SIZE = 64;
localparam L2_SIZE = 128;

typedef enum logic [2:0] {
    IDLE,
    FEATURE_EXTRACT,
    TEMPORAL_ACCUM,
    MLP_UPSAMPLE,
    FRAME_GEN,
    OUTPUT
} nsu_state_t;

nsu_state_t state, next_state;

reg [15:0] weight_ptr;
reg [7:0] layer;
reg [7:0] conv_cycle;
reg [31:0] feature_map [0:L1_SIZE-1];
reg [31:0] feature_map2 [0:L2_SIZE-1];
reg [31:0] accum_buffer [0:L1_SIZE-1];
reg [31:0] mlp_input [0:63];
reg [31:0] mlp_hidden [0:127];
reg [31:0] mlp_output [0:3];
reg [7:0] mlp_cycle;
reg [7:0] mlp_layer;
reg [31:0] pixel_in_reg;
reg [31:0] pixel_out_reg;
reg pixel_valid_reg;
reg weight_req_reg;
reg [15:0] weight_addr_reg;
reg [31:0] mac_accum [0:3];
reg [3:0] mac_cycle;
reg [31:0] prev_frame [0:L1_SIZE-1];

reg [31:0] optical_flow_x, optical_flow_y;
reg temporal_valid;

reg frame_gen_enable;
reg gen_frame_valid;
reg [31:0] gen_frame_pixel;

assign busy = (state != IDLE);
assign out_pixel = pixel_out_reg;
assign out_valid = pixel_valid_reg;
assign weight_addr = weight_addr_reg;
assign weight_req = weight_req_reg;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        state <= IDLE;
        weight_ptr <= 16'd0;
        layer <= 8'd0;
        conv_cycle <= 8'd0;
        mlp_cycle <= 8'd0;
        mlp_layer <= 8'd0;
        pixel_valid_reg <= 1'b0;
        pixel_out_reg <= 32'd0;
        weight_req_reg <= 1'b0;
        weight_addr_reg <= 16'd0;
        mac_cycle <= 4'd0;
        frame_gen_enable <= 1'b0;
        gen_frame_valid <= 1'b0;
        temporal_valid <= 1'b0;
        for (int i = 0; i < L1_SIZE; i = i + 1) begin
            feature_map[i] <= 32'd0;
            accum_buffer[i] <= 32'd0;
            prev_frame[i] <= 32'd0;
        end
        for (int i = 0; i < L2_SIZE; i = i + 1) begin
            feature_map2[i] <= 32'd0;
        end
        for (int i = 0; i < 64; i = i + 1) begin
            mlp_input[i] <= 32'd0;
            mlp_hidden[i] <= 32'd0;
            mlp_hidden[i+64] <= 32'd0;
        end
        for (int i = 0; i < 4; i = i + 1) begin
            mlp_output[i] <= 32'd0;
            mac_accum[i] <= 32'd0;
        end
    end else if (enable) begin
        case (state)
            IDLE: begin
                if (frame_valid) begin
                    pixel_in_reg <= frame_pixel;
                    weight_ptr <= 16'd0;
                    layer <= 8'd0;
                    conv_cycle <= 8'd0;
                    state <= FEATURE_EXTRACT;
                end
            end

            FEATURE_EXTRACT: begin
                if (layer < 8'd4) begin
                    if (conv_cycle < 8'd4) begin
                        weight_addr_reg <= weight_ptr;
                        weight_req_reg <= 1'b1;
                        if (weight_valid) begin
                            for (int i = 0; i < 4; i = i + 1) begin
                                mac_accum[i] <= 32'd0;
                                for (int k = 0; k < 4; k = k + 1) begin
                                    if (layer == 8'd0) begin
                                        if (i * 4 + k < L1_SIZE) begin
                                            mac_accum[i] <= mac_accum[i] +
                                                $signed(weight_rdata[k*32+:32]) *
                                                $signed(feature_map[i * 4 + k]);
                                        end
                                    end
                                end
                            end
                            weight_ptr <= weight_ptr + 16'd1;
                            conv_cycle <= conv_cycle + 8'd1;
                        end
                    end else begin
                        for (int i = 0; i < L1_SIZE; i = i + 1) begin
                            if (i < 4) begin
                                feature_map[i] <= mac_accum[i];
                                if (mac_accum[i][31] == 1'b0)
                                    feature_map[i] <= mac_accum[i];
                                else
                                    feature_map[i] <= 32'd0;
                            end
                        end
                        layer <= layer + 8'd1;
                        conv_cycle <= 8'd0;
                    end
                end else begin
                    state <= TEMPORAL_ACCUM;
                    temporal_valid <= 1'b1;
                end
                weight_req_reg <= 1'b0;
            end

            TEMPORAL_ACCUM: begin
                if (temporal_valid) begin
                    for (int i = 0; i < L1_SIZE; i = i + 1) begin
                        accum_buffer[i] <= feature_map[i] + prev_frame[i];
                    end
                    temporal_valid <= 1'b0;
                    state <= MLP_UPSAMPLE;
                    mlp_layer <= 8'd0;
                    mlp_cycle <= 8'd0;
                    for (int i = 0; i < 64; i = i + 1) begin
                        if (i < L1_SIZE)
                            mlp_input[i] <= feature_map[i];
                        else
                            mlp_input[i] <= 32'd0;
                    end
                end
            end

            MLP_UPSAMPLE: begin
                if (mlp_layer < 8'd4) begin
                    if (mlp_cycle < 8'd64) begin
                        weight_addr_reg <= weight_ptr;
                        weight_req_reg <= 1'b1;
                        if (weight_valid) begin
                            for (int i = 0; i < 4; i = i + 1) begin
                                if (mlp_layer == 8'd0) begin
                                    mac_accum[i] <= mac_accum[i] +
                                        $signed(weight_rdata[i*32+:32]) *
                                        $signed(mlp_input[mlp_cycle]);
                                end else if (mlp_layer == 8'd1) begin
                                    mac_accum[i] <= mac_accum[i] +
                                        $signed(weight_rdata[i*32+:32]) *
                                        $signed(mlp_hidden[mlp_cycle]);
                                end else if (mlp_layer == 8'd2) begin
                                    mac_accum[i] <= mac_accum[i] +
                                        $signed(weight_rdata[i*32+:32]) *
                                        $signed(mlp_hidden[mlp_cycle + 64]);
                                end
                            end
                            weight_ptr <= weight_ptr + 16'd1;
                            mlp_cycle <= mlp_cycle + 8'd1;
                        end
                    end else begin
                        if (mlp_layer == 8'd0) begin
                            for (int i = 0; i < 64; i = i + 1) begin
                                if (i < 4)
                                    mlp_hidden[i] <= mac_accum[i];
                                else
                                    mlp_hidden[i] <= 32'd0;
                            end
                        end else if (mlp_layer == 8'd1) begin
                            for (int i = 0; i < 64; i = i + 1) begin
                                if (i < 4)
                                    mlp_hidden[i+64] <= mac_accum[i];
                                else
                                    mlp_hidden[i+64] <= 32'd0;
                            end
                        end else if (mlp_layer == 8'd2) begin
                            for (int i = 0; i < 4; i = i + 1) begin
                                mlp_output[i] <= mac_accum[i];
                            end
                        end
                        mlp_layer <= mlp_layer + 8'd1;
                        mlp_cycle <= 8'd0;
                        for (int i = 0; i < 4; i = i + 1)
                            mac_accum[i] <= 32'd0;
                    end
                end else begin
                    pixel_out_reg <= mlp_output[0];
                    pixel_valid_reg <= 1'b1;
                    if (out_ready) begin
                        pixel_valid_reg <= 1'b0;
                        state <= IDLE;
                        for (int i = 0; i < L1_SIZE; i = i + 1)
                            prev_frame[i] <= feature_map[i];
                    end
                end
                weight_req_reg <= 1'b0;
            end

            default: state <= IDLE;
        endcase
    end
end

endmodule
