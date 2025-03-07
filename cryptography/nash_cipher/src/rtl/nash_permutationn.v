`timescale 1ns / 1ps

module nash_permutation_tb;

    // Test Inputs
    reg [7:0] index;
    
    // Test Outputs
    wire [7:0] red_next_state;
    wire red_transform;
    wire [7:0] blue_next_state;
    wire blue_transform;

    // Instantiate the Unit Under Test (UUT)
    nash_permutation_tables uut (
        .index(index),
        .red_next_state(red_next_state),
        .red_transform(red_transform),
        .blue_next_state(blue_next_state),
        .blue_transform(blue_transform)
    );

    // Test Sequence
    initial begin
        $display("Testing nash_permutation_tables...");
        $display("Index | Red Next State | Red Transform | Blue Next State | Blue Transform");
        $display("--------------------------------------------------------------");
        
        // Iterate over test indices
        for (index = 0; index < 10; index = index + 1) begin
            #10; // Wait for values to settle
            $display(" %3d  |      %3d      |       %b       |      %3d      |       %b",
                     index, red_next_state, red_transform, blue_next_state, blue_transform);
        end

        $display("--------------------------------------------------------------");
        $display("Test Completed.");
        $finish;
    end

endmodule
