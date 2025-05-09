module top_module (
    input wire clk,
    output wire led
);
    reg [23:0] counter = 0;

    always @(posedge clk) begin
        counter <= counter + 1;
    end

    assign led = counter[23];

endmodule
