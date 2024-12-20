module alu_miter(input [4:0] a, input [4:0] b, input [3:0] control, output result, output condition);

wire [9:0] alu_out;
wire [9:0] alu_golden_out;
wire [9:0] miter_out;

alu alu(.a(a), .b(b), .control(control), .out(alu_out));
alu_golden alu_golden(.a(a), .b(b), .out(alu_golden_out));

assign miter_out = alu_out ^ alu_golden_out;
assign result = |miter_out;
assign condition = (control == 4'b1000) ? 1'b1 : 1'b0;

endmodule
