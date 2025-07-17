module AdderMod2 #(
    parameter N = 128
)(
    input  wire [N-1:0] a,
    input  wire [N-1:0] b,
    output wire [N-1:0] sum
);
    assign sum = a ^ b; // Mod 2 addition
endmodule
