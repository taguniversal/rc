module nor_gate (
    input wire A,
    input wire B,
    output wire Y
);


wire or_output;

or_gate or1(
    .A(A),
    .B(B),
    .Y(or_output)
);


not_gate not1(
    .A(or_output),
    .Y(Y)
);

endmodule