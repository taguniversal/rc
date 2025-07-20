 module R30Array #(
    parameter N = 128
)(
    input wire [N-1:0] state_in,
    output wire [N-1:0] state_out
);

    // Array to hold each cell's output
    wire [N-1:0] cell_outputs;

    genvar i;
    generate
        for (i = 0; i < N; i = i + 1) begin : cell_array
            // Define local wires for this cell
            wire L_i;
            wire C_i;
            wire R_i;

            assign L_i = state_in[(i == 0) ? N-1 : i-1];
            assign C_i = state_in[i];
            assign R_i = state_in[(i == N-1) ? 0 : i+1];

            R30Cell cell_inst (
                .L(L_i),
                .C(C_i),
                .R(R_i),
                .R30CellOut(cell_outputs[i])
            );
        end
    endgenerate

    assign state_out = cell_outputs;

endmodule
