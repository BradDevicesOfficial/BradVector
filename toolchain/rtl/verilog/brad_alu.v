// SPDX-License-Identifier: MIT
// BradISA V1 -- ALU

`include "bradisa_defines.v"

module brad_alu (
    input  wire [31:0]  a,
    input  wire [31:0]  b,
    input  wire [3:0]   op,       // BRAD_OP_ADD ... BRAD_OP_SHR
    output reg  [31:0]  result
);

    always @(*) begin
        case (op)
            BRAD_OP_ADD: result = a + b;
            BRAD_OP_SUB: result = a - b;
            BRAD_OP_MUL: result = a * b;
            BRAD_OP_AND: result = a & b;
            BRAD_OP_OR:  result = a | b;
            BRAD_OP_XOR: result = a ^ b;
            BRAD_OP_SHL: result = a << b[4:0];
            BRAD_OP_SHR: result = a >> b[4:0];
            default:     result = 32'd0;
        endcase
    end

endmodule
