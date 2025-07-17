module tb_r30_cell();
    reg l = 0;
    reg c = 0;
    reg r = 0;
    wire y;

    // Instantiate the DUT (Device Under Test)
    R30Cell uut (
        .L(l),
        .C(c),
        .R(r),
        .R30CellOut(y)
    );

    initial begin
        $dumpfile("r30_cell_wave.vcd");
        $dumpvars(0, uut);

        // Test all 8 input combinations
        #10 l=0; c=0; r=0;
        #10 l=0; c=0; r=1;
        #10 l=0; c=1; r=0;
        #10 l=0; c=1; r=1;
        #10 l=1; c=0; r=0;
        #10 l=1; c=0; r=1;
        #10 l=1; c=1; r=0;
        #10 l=1; c=1; r=1;

        #10 $finish;
    end
endmodule
