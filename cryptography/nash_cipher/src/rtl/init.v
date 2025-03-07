// Initialization module for Nash cipher permutation tables
module nash_perm_init (
    input  wire        clk,
    input  wire        rst_n,
    output reg [3:0]   red_next_state  [0:15],
    output reg         red_transform   [0:15],
    output reg [3:0]   blue_next_state [0:15],
    output reg         blue_transform  [0:15]
);

    // Initialize permutation tables
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // Red permutation initialization (mapping the first 16 entries from the Cryptol)
            red_next_state[0]  <= 4'h4;  red_transform[0]  <= 0;  // 0b00010100 -> 4
            red_next_state[1]  <= 4'h8;  red_transform[1]  <= 1;  // 0b01011000 -> 8
            red_next_state[2]  <= 4'hB;  red_transform[2]  <= 1;  // 0b01011011 -> B
            red_next_state[3]  <= 4'h8;  red_transform[3]  <= 1;  // 0b00101000 -> 8
            red_next_state[4]  <= 4'hC;  red_transform[4]  <= 0;  // 0b01111100 -> C
            red_next_state[5]  <= 4'hE;  red_transform[5]  <= 1;  // 0b01101110 -> E
            red_next_state[6]  <= 4'h7;  red_transform[6]  <= 0;  // 0b00000111 -> 7
            red_next_state[7]  <= 4'hD;  red_transform[7]  <= 0;  // 0b01111101 -> D
            red_next_state[8]  <= 4'h2;  red_transform[8]  <= 0;  // 0b01100010 -> 2
            red_next_state[9]  <= 4'h3;  red_transform[9]  <= 0;  // 0b00100011 -> 3
            red_next_state[10] <= 4'hF;  red_transform[10] <= 1;  // 0b00001111 -> F
            red_next_state[11] <= 4'hF;  red_transform[11] <= 1;  // 0b01001111 -> F
            red_next_state[12] <= 4'hF;  red_transform[12] <= 0;  // 0b00111111 -> F
            red_next_state[13] <= 4'hB;  red_transform[13] <= 1;  // 0b00101011 -> B
            red_next_state[14] <= 4'h0;  red_transform[14] <= 0;  // 0b01100000 -> 0
            red_next_state[15] <= 4'hE;  red_transform[15] <= 1;  // 0b01111110 -> E

            // Blue permutation initialization (mapping the first 16 entries from the Cryptol)
            blue_next_state[0]  <= 4'h4;  blue_transform[0]  <= 0;  // 0b00010100 -> 4
            blue_next_state[1]  <= 4'hB;  blue_transform[1]  <= 0;  // 0b00101011 -> B
            blue_next_state[2]  <= 4'h9;  blue_transform[2]  <= 1;  // 0b00011001 -> 9
            blue_next_state[3]  <= 4'hE;  blue_transform[3]  <= 1;  // 0b00011110 -> E
            blue_next_state[4]  <= 4'h2;  blue_transform[4]  <= 1;  // 0b00000010 -> 2
            blue_next_state[5]  <= 4'h2;  blue_transform[5]  <= 1;  // 0b01000010 -> 2
            blue_next_state[6]  <= 4'h8;  blue_transform[6]  <= 0;  // 0b00101000 -> 8
            blue_next_state[7]  <= 4'hB;  blue_transform[7]  <= 1;  // 0b01001011 -> B
            blue_next_state[8]  <= 4'hB;  blue_transform[8]  <= 0;  // 0b00011011 -> B
            blue_next_state[9]  <= 4'hD;  blue_transform[9]  <= 0;  // 0b01101101 -> D
            blue_next_state[10] <= 4'hE;  blue_transform[10] <= 0;  // 0b00101110 -> E
            blue_next_state[11] <= 4'h3;  blue_transform[11] <= 0;  // 0b01000011 -> 3
            blue_next_state[12] <= 4'h1;  blue_transform[12] <= 1;  // 0b01010001 -> 1
            blue_next_state[13] <= 4'h4;  blue_transform[13] <= 1;  // 0b00100100 -> 4
            blue_next_state[14] <= 4'hD;  blue_transform[14] <= 0;  // 0b00111101 -> D
            blue_next_state[15] <= 4'hD;  blue_transform[15] <= 0;  // 0b01011101 -> D
        end
    end

endmodule

// Updated testbench to use 4-bit state width
module nash_cipher_tb;
    reg         clk;
    reg         rst_n;
    reg         input_bit;
    reg  [7:0]  key_data;
    wire        cipher_bit;
    wire        valid;

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Instantiate the cipher
    nash_cipher_top #(
        .STATE_WIDTH(4),  // Use 4-bit state width for initial testing
        .MEM_DEPTH(8)
    ) uut (
        .clk(clk),
        .rst_n(rst_n),
        .input_bit(input_bit),
        .key_data(key_data),
        .cipher_bit(cipher_bit),
        .valid(valid)
    );

    // Test stimulus
    initial begin
        // Initialize signals
        rst_n = 0;
        input_bit = 0;
        key_data = 8'h55;  // Example key

        // Reset sequence
        #20 rst_n = 1;

        // Test pattern
        #10 input_bit = 1;  // Should use red permutation
        #10 input_bit = 0;  // Should use blue permutation
        #10 input_bit = 1;
        #10 input_bit = 1;

        // Monitor state
        #10 if (!valid) $display("Error: Valid signal not asserted!");
        
        // Display output
        $display("Time=%0t cipher_bit=%b", $time, cipher_bit);

        #100 $finish;
    end

    // Optional: Waveform dump
    initial begin
        $dumpfile("nash_cipher.vcd");
        $dumpvars(0, nash_cipher_tb);
    end

endmodule