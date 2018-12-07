// Register Unit (for simulation)

`timescale 1ns / 1ps

module register(
    input clk,
    input reset,
    input [15:0] in,
    output [15:0] out
    );

reg [15:0] data;
assign out[15:0] = data[15:0];

always @(posedge clk) begin
	if (reset) begin
		data[15:0] <= 16'b0000000000000000;
	end
	else begin
		data[15:0] <= in[15:0];
	end

end

endmodule
