module R30Field #(
    parameter N = 128,       // Width of the array
    parameter D = 256        // Depth: number of steps
)(
    input  wire [N-1:0] seed,
    output wire [N-1:0] final_state,
    output wire [127:0] column_slice
);

    wire [N-1:0] states [0:D];

    assign states[0] = seed;

    genvar i;
    generate
        for (i = 0; i < D; i = i + 1) begin : r30_steps
            R30Array #(.N(N)) step (
                .state_in(states[i]),
                .state_out(states[i+1])
            );
        end
    endgenerate

    assign final_state = states[D];

    // Expose center column bits from steps 128 to 255
    genvar j;
    generate
        for (j = 0; j < 128; j = j + 1) begin : extract_column
            assign column_slice[j] = states[128 + j][N/2];
        end
    endgenerate

endmodule
