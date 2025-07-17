module NashCipher (
    input wire clk,
    input wire reset,
    input wire input_bit,
    input wire decider_bit,
    output reg output_bit
);

    // State and memory arrays
    reg [6:0] current_state = 0;  // 7 bits to represent 0–127

    reg [6:0] red_permuter [0:127];
    reg [6:0] blue_permuter [0:127];
    reg flip_matrix [0:127];

    integer i;

    // Optional: initialize the permuters and matrix (can be overridden externally)
    initial begin
        for (i = 0; i < 128; i = i + 1) begin
            red_permuter[i] = i;    // Identity by default
            blue_permuter[i] = i;
            flip_matrix[i] = 0;     // No flipping by default
        end
    end

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            current_state <= 0;
        end else begin
            // Update state based on decider bit
            if (decider_bit == 1'b0)
                current_state <= red_permuter[current_state];
            else
                current_state <= blue_permuter[current_state];

            // Flip or pass through
            if (flip_matrix[current_state])
                output_bit <= ~input_bit;
            else
                output_bit <= input_bit;
        end
    end

endmodule
