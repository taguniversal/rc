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
    reg       red_flip_matrix  [0:127];
    reg       blue_flip_matrix [0:127];

    integer i;

    // Optional: initialize permuters and flip matrices (can be overridden externally)
    initial begin
        for (i = 0; i < 128; i = i + 1) begin
            red_permuter[i]       = i;  // Identity permutation
            blue_permuter[i]      = i;
            red_flip_matrix[i]    = 0;  // No flipping by default
            blue_flip_matrix[i]   = 0;
        end
    end

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            current_state <= 0;
        end else begin
            // Choose next state and flip matrix based on decider bit
            if (decider_bit == 1'b0) begin
                current_state <= red_permuter[current_state];
                output_bit <= red_flip_matrix[current_state] ? ~input_bit : input_bit;
            end else begin
                current_state <= blue_permuter[current_state];
                output_bit <= blue_flip_matrix[current_state] ? ~input_bit : input_bit;
            end
        end
    end

endmodule
