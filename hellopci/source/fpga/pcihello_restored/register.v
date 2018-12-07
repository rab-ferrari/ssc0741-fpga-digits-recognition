// Register Unit (for simulation)

`timescale 1ns / 1ps

module register(
    input clk,
    input reset,
    input [31:0] in,
    output [31:0] out
    );

reg [31:0] data;
assign out[31:0] = data[31:0];

always @(posedge clk) begin
	if (reset) begin
		data[31:0] <= 32'b0;
	end
	else begin
		data[31:0] <= in[31:0];
	end

end

endmodule
