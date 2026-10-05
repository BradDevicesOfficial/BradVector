`timescale 1ns / 1ps

module bradgfx_top #(
    parameter NUM_VEC = 64,
    parameter NUM_RT  = 32,
    parameter NUM_NSU = 16,
    parameter SKU_ID  = 0
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire [31:0]  mmio_addr,
    input  wire [31:0]  mmio_wdata,
    output wire [31:0]  mmio_rdata,
    input  wire         mmio_write,
    input  wire         mmio_read,
    output wire         mmio_ready,
    output wire [47:0]  fabric_addr,
    output wire [511:0] fabric_wdata,
    input  wire [511:0] fabric_rdata,
    output wire         fabric_write,
    output wire         fabric_read,
    input  wire         fabric_ready,
    output wire         fabric_valid,
    output wire [8:0]   irq,
    input  wire [2:0]   pwr_state,
    output wire         pwr_ack
);

localparam NUM_IRQ = 9;
localparam CMDQ_SIZE = 1024;

reg [31:0] gfx_ctrl;
reg [31:0] gfx_status;
reg [31:0] gfx_pwr_state;
reg [31:0] gfx_clk_gate;
reg [31:0] gfx_intr_enable;
reg [31:0] gfx_intr_status;
reg [31:0] gfx_vrs_ctrl;
reg [31:0] gfx_ray_ctrl;
reg [31:0] gfx_bradsense_ctrl;
reg [31:0] gfx_voltage;
reg [31:0] gfx_freq;
reg [31:0] gfx_pwr_limit;
reg [31:0] gfx_temp_limit;
reg [31:0] gfx_perf_cntr_cfg;

reg [63:0] gfx_perf_cntr_0;
reg [63:0] gfx_perf_cntr_1;

reg [63:0] cmd_queue_base;
reg [31:0] cmd_queue_size;
reg [31:0] cmd_queue_head;
reg [31:0] cmd_queue_tail;
reg [31:0] cmd_doorbell;

reg [63:0] inf_buf_base;
reg [63:0] inf_buf_limit;

reg [31:0] mmio_rdata_reg;
reg mmio_ready_reg;

reg [NUM_VEC-1:0] vec_enable;
reg [NUM_RT-1:0] rt_enable;
reg [NUM_NSU-1:0] nsu_enable;

reg [NUM_IRQ-1:0] irq_pending;
reg [NUM_IRQ-1:0] irq_mask;

reg busy_flag;
reg [7:0] vec_active_count;
reg [7:0] rt_active_count;
reg [7:0] nsu_active_count;

reg [47:0] fabric_addr_reg;
reg [511:0] fabric_wdata_reg;
reg fabric_write_reg;
reg fabric_read_reg;
reg fabric_valid_reg;

reg pwr_transition;
reg [3:0] pwr_delay;

wire [31:0] sku_revision = {8'd1, 8'd0, 8'd0, 8'd1};
wire [31:0] sku_id = SKU_ID;
wire [31:0] num_vec = NUM_VEC;
wire [31:0] num_rt = NUM_RT;
wire [31:0] num_nsu = NUM_NSU;

wire [31:0] fabric_bw;
assign fabric_bw = (SKU_ID == 3) ? 32'd2000000 : 32'd1000000;

genvar v;
generate
    for (v = 0; v < NUM_VEC; v = v + 1) begin : gen_vec_cores
        bradvector_core vec_inst (
            .clk(clk),
            .rst_n(rst_n && vec_enable[v]),
            .enable(vec_enable[v]),
            .warp_id(v[4:0]),
            .ifetch_addr(),
            .ifetch_data(128'd0),
            .ifetch_valid(1'b0),
            .ifetch_ready(),
            .mem_addr(),
            .mem_wdata(),
            .mem_rdata(256'd0),
            .mem_write(),
            .mem_read(),
            .mem_ready(1'b0),
            .busy(),
            .perf_cnt()
        );
    end
endgenerate

genvar r;
generate
    for (r = 0; r < NUM_RT; r = r + 1) begin : gen_rt_cores
        bradrt_core rt_inst (
            .clk(clk),
            .rst_n(rst_n && rt_enable[r]),
            .enable(rt_enable[r]),
            .ray_origin(96'd0),
            .ray_direction(96'd0),
            .ray_valid(1'b0),
            .ray_ready(),
            .bvh_addr(),
            .bvh_data(128'd0),
            .bvh_valid(1'b0),
            .hit(),
            .hit_tri_id(),
            .hit_distance(),
            .hit_barycentric(),
            .result_valid(),
            .result_ready(1'b0),
            .busy()
        );
    end
endgenerate

genvar n;
generate
    for (n = 0; n < NUM_NSU; n = n + 1) begin : gen_nsu_cores
        neurostream_core nsu_inst (
            .clk(clk),
            .rst_n(rst_n && nsu_enable[n]),
            .enable(nsu_enable[n]),
            .quality_mode(gfx_bradsense_ctrl[1:0]),
            .weight_addr(),
            .weight_rdata(128'd0),
            .weight_req(),
            .weight_valid(1'b0),
            .frame_pixel(32'd0),
            .frame_valid(1'b0),
            .out_pixel(),
            .out_valid(),
            .out_ready(1'b0),
            .busy()
        );
    end
endgenerate

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        gfx_ctrl <= 32'd0;
        gfx_status <= 32'd0;
        gfx_pwr_state <= 32'd1;
        gfx_clk_gate <= 32'hFFFFFFFF;
        gfx_intr_enable <= 32'd0;
        gfx_intr_status <= 32'd0;
        gfx_vrs_ctrl <= 32'd0;
        gfx_ray_ctrl <= 32'd0;
        gfx_bradsense_ctrl <= 32'd0;
        gfx_voltage <= 32'd0;
        gfx_freq <= 32'd0;
        gfx_pwr_limit <= 32'd250;
        gfx_temp_limit <= 32'd95;
        gfx_perf_cntr_cfg <= 32'd0;
        gfx_perf_cntr_0 <= 64'd0;
        gfx_perf_cntr_1 <= 64'd0;
        cmd_queue_base <= 64'd0;
        cmd_queue_size <= 32'd0;
        cmd_queue_head <= 32'd0;
        cmd_queue_tail <= 32'd0;
        cmd_doorbell <= 32'd0;
        inf_buf_base <= 64'd0;
        inf_buf_limit <= 64'd0;
        vec_enable <= {NUM_VEC{1'b0}};
        rt_enable <= {NUM_RT{1'b0}};
        nsu_enable <= {NUM_NSU{1'b0}};
        irq_pending <= {NUM_IRQ{1'b0}};
        irq_mask <= {NUM_IRQ{1'b0}};
        busy_flag <= 1'b0;
        mmio_ready_reg <= 1'b0;
        mmio_rdata_reg <= 32'd0;
        pwr_transition <= 1'b0;
        pwr_delay <= 4'd0;
        pwr_ack <= 1'b0;
        fabric_valid_reg <= 1'b0;
        fabric_write_reg <= 1'b0;
        fabric_read_reg <= 1'b0;
    end else begin
        mmio_ready_reg <= 1'b0;

        if (pwr_transition) begin
            if (pwr_delay < 4'd8) begin
                pwr_delay <= pwr_delay + 4'd1;
            end else begin
                pwr_transition <= 1'b0;
                pwr_ack <= 1'b1;
            end
        end else begin
            pwr_ack <= 1'b0;
        end

        if (gfx_ctrl[1]) begin
            gfx_ctrl[1] <= 1'b0;
            gfx_status[0] <= 1'b0;
        end

        if (mmio_write && !mmio_ready_reg) begin
            mmio_ready_reg <= 1'b1;
            case (mmio_addr[15:2])
                14'h0000: begin
                    gfx_ctrl <= mmio_wdata;
                    if (mmio_wdata[0]) begin
                        gfx_status[0] <= 1'b1;
                        vec_enable <= {NUM_VEC{mmio_wdata[2]}};
                        rt_enable <= {NUM_RT{gfx_ray_ctrl[0]}};
                        nsu_enable <= {NUM_NSU{gfx_ctrl[3]}};
                    end
                end
                14'h0001: gfx_status <= mmio_wdata;
                14'h0008: vec_enable <= {NUM_VEC{mmio_wdata[0]}};
                14'h0009: rt_enable <= {NUM_RT{mmio_wdata[0]}};
                14'h000A: nsu_enable <= {NUM_NSU{mmio_wdata[0]}};
                14'h000B: gfx_pwr_state <= mmio_wdata;
                14'h000C: gfx_clk_gate <= mmio_wdata;
                14'h000D: gfx_intr_enable <= mmio_wdata;
                14'h000E: begin
                    gfx_intr_status <= gfx_intr_status & ~mmio_wdata;
                    irq_pending <= irq_pending & ~mmio_wdata[NUM_IRQ-1:0];
                end
                14'h000F: gfx_vrs_ctrl <= mmio_wdata;
                14'h0010: gfx_ray_ctrl <= mmio_wdata;
                14'h0011: gfx_bradsense_ctrl <= mmio_wdata;
                14'h0012: gfx_voltage <= mmio_wdata;
                14'h0013: gfx_freq <= mmio_wdata;
                14'h0014: gfx_pwr_limit <= mmio_wdata;
                14'h0015: gfx_temp_limit <= mmio_wdata;
                14'h0016: gfx_perf_cntr_cfg <= mmio_wdata;
                14'h0400: cmd_queue_base[31:0] <= mmio_wdata;
                14'h0401: cmd_queue_base[63:32] <= mmio_wdata;
                14'h0402: cmd_queue_size <= mmio_wdata;
                14'h0403: cmd_queue_tail <= mmio_wdata;
                14'h0404: begin
                    cmd_doorbell <= cmd_doorbell + 32'd1;
                    busy_flag <= 1'b1;
                end
                14'h0017: begin
                    inf_buf_base[31:0] <= mmio_wdata;
                end
            endcase
        end

        if (mmio_read && !mmio_ready_reg) begin
            mmio_ready_reg <= 1'b1;
            case (mmio_addr[15:2])
                14'h0000: mmio_rdata_reg <= gfx_ctrl;
                14'h0001: mmio_rdata_reg <= gfx_status;
                14'h0002: mmio_rdata_reg <= sku_revision;
                14'h0003: mmio_rdata_reg <= sku_id;
                14'h0004: mmio_rdata_reg <= num_vec;
                14'h0005: mmio_rdata_reg <= num_rt;
                14'h0006: mmio_rdata_reg <= num_nsu;
                14'h0007: mmio_rdata_reg <= fabric_bw;
                14'h0008: mmio_rdata_reg <= vec_enable;
                14'h0009: mmio_rdata_reg <= rt_enable;
                14'h000A: mmio_rdata_reg <= nsu_enable;
                14'h000B: mmio_rdata_reg <= gfx_pwr_state;
                14'h000C: mmio_rdata_reg <= gfx_clk_gate;
                14'h000D: mmio_rdata_reg <= gfx_intr_enable;
                14'h000E: mmio_rdata_reg <= gfx_intr_status;
                14'h000F: mmio_rdata_reg <= gfx_vrs_ctrl;
                14'h0010: mmio_rdata_reg <= gfx_ray_ctrl;
                14'h0011: mmio_rdata_reg <= gfx_bradsense_ctrl;
                14'h0012: mmio_rdata_reg <= gfx_voltage;
                14'h0013: mmio_rdata_reg <= gfx_freq;
                14'h0014: mmio_rdata_reg <= gfx_pwr_limit;
                14'h0015: mmio_rdata_reg <= gfx_temp_limit;
                14'h0016: mmio_rdata_reg <= gfx_perf_cntr_cfg;
                14'h0017: mmio_rdata_reg <= gfx_perf_cntr_0[31:0];
                14'h0018: mmio_rdata_reg <= gfx_perf_cntr_1[31:0];
                14'h0400: mmio_rdata_reg <= cmd_queue_head;
                14'h0401: mmio_rdata_reg <= cmd_queue_tail;
                14'h0402: mmio_rdata_reg <= cmd_queue_base[31:0];
                14'h0403: mmio_rdata_reg <= cmd_queue_size;
                default: mmio_rdata_reg <= 32'd0;
            endcase
        end

        if (gfx_ctrl[0] && gfx_perf_cntr_cfg[0]) begin
            gfx_perf_cntr_0 <= gfx_perf_cntr_0 + 64'd1;
        end

        if (gfx_intr_enable[0] && !irq_mask[0]) begin
            irq_pending[0] <= 1'b1;
            irq_mask[0] <= 1'b1;
        end

        if (irq_pending[0] && gfx_intr_enable[0]) begin
            gfx_intr_status[0] <= 1'b1;
        end
    end
end

assign mmio_rdata = mmio_rdata_reg;
assign mmio_ready = mmio_ready_reg;
assign irq = irq_pending[NUM_IRQ-1:0] & gfx_intr_enable[NUM_IRQ-1:0];
assign fabric_addr = fabric_addr_reg;
assign fabric_wdata = fabric_wdata_reg;
assign fabric_write = fabric_write_reg;
assign fabric_read = fabric_read_reg;
assign fabric_valid = fabric_valid_reg;

endmodule
