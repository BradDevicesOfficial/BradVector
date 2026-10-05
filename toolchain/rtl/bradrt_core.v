`timescale 1ns / 1ps

module bradrt_core (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         enable,
    input  wire [95:0]  ray_origin,
    input  wire [95:0]  ray_direction,
    input  wire         ray_valid,
    output wire         ray_ready,
    output wire [31:0]  bvh_addr,
    input  wire [127:0] bvh_data,
    input  wire         bvh_valid,
    output wire         hit,
    output wire [31:0]  hit_tri_id,
    output wire [31:0]  hit_distance,
    output wire [95:0]  hit_barycentric,
    output wire         result_valid,
    input  wire         result_ready,
    output wire         busy
);

localparam STACK_DEPTH = 64;
localparam BVH_CACHE_LINES = 64;

typedef enum logic [2:0] {
    IDLE,
    BVH_TRAVERSE,
    TRI_INTERSECT,
    PRP_EXEC,
    NERF_EVAL,
    WRITEBACK
} rt_state_t;

rt_state_t state, next_state;

reg [31:0] traversal_stack [0:STACK_DEPTH-1];
reg [5:0] stack_ptr;
reg [31:0] node_addr;
reg [31:0] closest_tri;
reg [31:0] closest_dist;
reg [95:0] closest_bary;
reg [31:0] org_x, org_y, org_z;
reg [31:0] dir_x, dir_y, dir_z;
reg [31:0] inv_dir_x, inv_dir_y, inv_dir_z;
reg [31:0] tmin, tmax;
reg [31:0] tri_v0_x, tri_v0_y, tri_v0_z;
reg [31:0] tri_v1_x, tri_v1_y, tri_v1_z;
reg [31:0] tri_v2_x, tri_v2_y, tri_v2_z;
reg bvh_requested;
reg bvh_waiting;

reg [7:0] nerf_layer;
reg [7:0] nerf_cycle;
reg [63:0] nerf_hidden [0:63];
reg [63:0] nerf_weights_0 [0:63];
reg [63:0] nerf_weights_1 [0:63];
reg [63:0] nerf_weights_2 [0:63];
reg [63:0] nerf_weights_3 [0:3];
reg nerf_busy;
reg [31:0] nerf_result [0:3];

assign busy = (state != IDLE) || |nerf_busy;
assign ray_ready = (state == IDLE) && !ray_valid;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        state <= IDLE;
        stack_ptr <= 6'd0;
        node_addr <= 32'd0;
        closest_tri <= 32'd0;
        closest_dist <= 32'h7F7FFFFF;
        tmin <= 32'd0;
        tmax <= 32'h7F7FFFFF;
        bvh_requested <= 1'b0;
        bvh_waiting <= 1'b0;
        nerf_busy <= 1'b0;
        nerf_layer <= 8'd0;
        nerf_cycle <= 8'd0;
    end else begin
        case (state)
            IDLE: begin
                if (ray_valid && enable) begin
                    org_x <= ray_origin[95:64];
                    org_y <= ray_origin[63:32];
                    org_z <= ray_origin[31:0];
                    dir_x <= ray_direction[95:64];
                    dir_y <= ray_direction[63:32];
                    dir_z <= ray_direction[31:0];
                    if (ray_direction[95:64] != 32'd0)
                        inv_dir_x <= 32'h3F800000 / ray_direction[95:64];
                    if (ray_direction[63:32] != 32'd0)
                        inv_dir_y <= 32'h3F800000 / ray_direction[63:32];
                    if (ray_direction[31:0] != 32'd0)
                        inv_dir_z <= 32'h3F800000 / ray_direction[31:0];
                    tmin <= 32'd0;
                    tmax <= 32'h7F7FFFFF;
                    stack_ptr <= 6'd1;
                    traversal_stack[0] <= 32'd0;
                    node_addr <= 32'd0;
                    closest_dist <= 32'h7F7FFFFF;
                    closest_tri <= 32'd0;
                    bvh_requested <= 1'b1;
                    state <= BVH_TRAVERSE;
                end
            end

            BVH_TRAVERSE: begin
                if (bvh_requested && bvh_valid) begin
                    bvh_requested <= 1'b0;
                    node_addr <= node_addr + 32'd16;

                    if (bvh_data[127]) begin
                        reg [31:0] child0, child1;
                        child0 <= bvh_data[95:64];
                        child1 <= bvh_data[63:32];
                        reg do_child0, do_child1;
                        do_child0 = 1'b0; do_child1 = 1'b0;
                        if (bvh_valid) begin
                            do_child0 = 1'b1;
                            do_child1 = 1'b1;
                        end
                        if (do_child0 && do_child1) begin
                            traversal_stack[stack_ptr] <= child1;
                            stack_ptr <= stack_ptr + 6'd1;
                            node_addr <= child0;
                            bvh_requested <= 1'b1;
                        end else if (do_child0) begin
                            node_addr <= child0;
                            bvh_requested <= 1'b1;
                        end else if (do_child1) begin
                            node_addr <= child1;
                            bvh_requested <= 1'b1;
                        end else begin
                            if (stack_ptr > 6'd0) begin
                                stack_ptr <= stack_ptr - 6'd1;
                                node_addr <= traversal_stack[stack_ptr - 6'd1];
                                bvh_requested <= 1'b1;
                            end else begin
                                state <= WRITEBACK;
                            end
                        end
                    end else begin
                        tri_v0_x <= bvh_data[95:64];
                        tri_v0_y <= bvh_data[63:32];
                        tri_v0_z <= bvh_data[31:0];
                        if (bvh_valid) begin
                            state <= TRI_INTERSECT;
                        end
                    end
                end

                if (!bvh_requested && !bvh_valid) begin
                    bvh_addr <= node_addr;
                    bvh_requested <= 1'b1;
                end
            end

            TRI_INTERSECT: begin
                reg [31:0] e1x, e1y, e1z, e2x, e2y, e2z;
                reg [31:0] tvec_x, tvec_y, tvec_z;
                reg [31:0] pvec_x, pvec_y, pvec_z;
                reg [31:0] det, inv_det;
                reg [31:0] u, v;
                reg [31:0] t;

                e1x = tri_v1_x - tri_v0_x;
                e1y = tri_v1_y - tri_v0_y;
                e1z = tri_v1_z - tri_v0_z;
                e2x = tri_v2_x - tri_v0_x;
                e2y = tri_v2_y - tri_v0_y;
                e2z = tri_v2_z - tri_v0_z;

                pvec_x = dir_y * e2z - dir_z * e2y;
                pvec_y = dir_z * e2x - dir_x * e2z;
                pvec_z = dir_x * e2y - dir_y * e2x;

                det = e1x * pvec_x + e1y * pvec_y + e1z * pvec_z;

                if (det > 32'h3A800000 || det < -32'h3A800000) begin
                    inv_det = $rtoi(1.0 / $bitstoreal(det));
                    tvec_x = org_x - tri_v0_x;
                    tvec_y = org_y - tri_v0_y;
                    tvec_z = org_z - tri_v0_z;
                    u = (tvec_x * pvec_x + tvec_y * pvec_y + tvec_z * pvec_z);

                    if (u >= 0 && u <= det) begin
                        reg [31:0] qvec_x, qvec_y, qvec_z;
                        qvec_x = tvec_y * e1z - tvec_z * e1y;
                        qvec_y = tvec_z * e1x - tvec_x * e1z;
                        qvec_z = tvec_x * e1y - tvec_y * e1x;
                        v = (dir_x * qvec_x + dir_y * qvec_y + dir_z * qvec_z);
                        if (v >= 0 && (u + v) <= det) begin
                            t = (e2x * qvec_x + e2y * qvec_y + e2z * qvec_z);
                            if (t > tmin && t < closest_dist) begin
                                closest_dist <= t;
                                closest_tri <= node_addr;
                                closest_bary[95:64] <= u;
                                closest_bary[63:32] <= v;
                                closest_bary[31:0] <= (det - u - v);
                            end
                        end
                    end
                end

                if (stack_ptr > 6'd0) begin
                    stack_ptr <= stack_ptr - 6'd1;
                    node_addr <= traversal_stack[stack_ptr - 6'd1];
                    bvh_requested <= 1'b1;
                    state <= BVH_TRAVERSE;
                end else begin
                    state <= WRITEBACK;
                end
            end

            WRITEBACK: begin
                state <= IDLE;
            end

            default: state <= IDLE;
        endcase
    end
end

assign hit = (closest_dist < 32'h7F7FFFFF);
assign hit_tri_id = closest_tri;
assign hit_distance = closest_dist;
assign hit_barycentric = closest_bary;
assign result_valid = (state == WRITEBACK);
assign bvh_addr = node_addr;

endmodule
