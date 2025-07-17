module tb_nor();
    reg a = 0;
    reg b = 0;
    wire y;

    nor_gate uut (
        .A(a),
        .B(b),
        .Y(y)
    );

    initial begin
        $dumpfile("nor_wave.vcd");
        $dumpvars(0, uut);

        #10 a=0; b=0;
        #10 a=0; b=1;
        #10 a=1; b=0;
        #10 a=1; b=1;
        #10 $finish;
    end
endmodule
