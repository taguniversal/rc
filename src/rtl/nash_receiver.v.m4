module NashReceiver #(
    parameter N = 128
)(
    input  wire [N-1:0] ciphertext,
    input  wire [N-1:0] seed,
    output wire [N-1:0] plaintext
);
    wire [N-1:0] permutation;

    R30Field #(.N(N), .D(256)) perm (
        .seed(seed),
        .final_state(permutation),
        .column_slice()
    );

    AdderMod2 #(.N(N)) add (
        .a(ciphertext),
        .b(permutation),
        .sum(plaintext)
    );
endmodule
