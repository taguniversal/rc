module blink (
    input CLK,
    output LED
);
    reg [23:0] counter = 0;
    always @(posedge CLK) begin
        counter <= counter + 1;
    end
    assign LED = counter[23];
endmodule
