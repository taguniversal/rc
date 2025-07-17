module R30EntropyGen #(
    parameter N = 128
)(
    input  wire [N-1:0] seed,
    output wire [N-1:0] next_seed
);

    wire [N-1:0] final_state;
    wire [N-1:0] column_slice;

    R30Field #(.N(N), .D(256)) field (
        .seed(seed),
        .final_state(final_state),
        .column_slice(column_slice)
    );

    assign next_seed = seed ^ column_slice;

endmodule
