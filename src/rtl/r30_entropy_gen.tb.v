module tb_R30EntropyGen;

    parameter N = 128;
    reg  [N-1:0] seed;
    wire [N-1:0] next_seed;

    R30EntropyGen #(.N(N)) uut (
        .seed(seed),
        .next_seed(next_seed)
    );

    initial begin
        $dumpfile("entropy.vcd");
        $dumpvars(0, tb_R30EntropyGen);

        seed = 0;
        seed[N/2] = 1'b1;  // single center bit

        #100;  // allow propagation

        $display("Next seed: %b", next_seed);

        #10;
        $finish;
    end

endmodule
