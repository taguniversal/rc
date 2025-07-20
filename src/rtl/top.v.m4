include(`macros.m4')dnl

module top(
  input clk,
  output [127:0] psi_out
);

assign psi_out = PSIplus(4);

endmodule
