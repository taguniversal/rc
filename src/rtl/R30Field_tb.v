module tb_R30Field;

    parameter N = 128;
    parameter D = 256;

    reg [N-1:0] seed;
    wire [N-1:0] result;

    R30Field #(.N(N), .D(D)) uut (
        .seed(seed),
        .final_state(result)
    );

    initial begin
        // Seed: single 1 in center (classic entropy trigger)
        seed = 0;
        seed[N/2] = 1'b1;

        #10; // Let combinatorial net settle

        $dumpfile("wave.vcd");
        $dumpvars(0, tb_R30Field);

        #100; // Just enough time for GTKWave capture
        $finish;
    end

endmodule
