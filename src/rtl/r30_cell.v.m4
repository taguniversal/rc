module R30Cell (
    input wire L,
    input wire C,
    input wire R,
    output wire R30CellOut
);

assign R30CellOut = L ^ (C | R);

endmodule
