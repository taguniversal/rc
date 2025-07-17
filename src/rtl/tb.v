module tb();
    reg a = 0, b = 0;
    wire y;

    and_gate uut (
        .a(a),
        .b(b),
        .y(y)
    );

    

    initial begin
        $dumpfile("wave.vcd");
        $dumpvars(0, uut);

        a = 0; b = 0; #10;
        a = 1; b = 0; #10;
        a = 0; b = 1; #10;
        a = 1; b = 1; #10;

        $finish;
    end
endmodule
