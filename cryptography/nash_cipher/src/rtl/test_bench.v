`timescale 1ns / 1ps

module nash_cipher_tb;
    // Test bench signals
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
    nash_cipher_top uut (
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

        // Test single bit encryption
        #10 input_bit = 1;
        #10 input_bit = 0;
        #10 input_bit = 1;
        #10 input_bit = 1;

        // Wait for completion
        #100;
        $finish;
    end

    // Monitor
    initial begin
        $monitor("Time=%0t rst_n=%b input=%b key=0x%h cipher=%b valid=%b", 
                 $time, rst_n, input_bit, key_data, cipher_bit, valid);
    end

endmodule

// Decryption testbench - test both encryption and decryption together
module nash_cipher_full_tb;
    // Test bench signals for encryptor
    reg         clk;
    reg         rst_n;
    reg         enc_input_bit;
    reg  [7:0]  enc_key_data;
    wire        enc_cipher_bit;
    wire        enc_valid;

    // Test bench signals for decryptor
    reg  [7:0]  dec_key_data;
    wire        dec_output_bit;
    wire        dec_valid;

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // Instantiate encryption unit
    nash_cipher_top encryptor (
        .clk(clk),
        .rst_n(rst_n),
        .input_bit(enc_input_bit),
        .key_data(enc_key_data),
        .cipher_bit(enc_cipher_bit),
        .valid(enc_valid)
    );

    // Instantiate decryption unit
    nash_cipher_top decryptor (
        .clk(clk),
        .rst_n(rst_n),
        .input_bit(enc_cipher_bit),  // Feed cipher text from encryptor
        .key_data(dec_key_data),
        .cipher_bit(dec_output_bit),
        .valid(dec_valid)
    );

    // Test vectors
    reg [15:0] test_data;
    integer i;

    initial begin
        // Initialize signals
        rst_n = 0;
        enc_input_bit = 0;
        enc_key_data = 8'h55;  // Example key
        dec_key_data = 8'h55;  // Same key for decrypt
        test_data = 16'hA5A5;  // Test pattern

        // Reset sequence
        #20 rst_n = 1;

        // Send test pattern through encryption/decryption
        for(i = 0; i < 16; i = i + 1) begin
            #10 enc_input_bit = test_data[i];
        end

        // Wait for pipeline to flush
        #100;
        $finish;
    end

    // Monitor encryption/decryption process
    initial begin
        $monitor("Time=%0t input=%b cipher=%b recovered=%b valid_enc=%b valid_dec=%b", 
                 $time, enc_input_bit, enc_cipher_bit, dec_output_bit, 
                 enc_valid, dec_valid);
    end

    // Optional: Save waveforms
    initial begin
        $dumpfile("nash_cipher.vcd");
        $dumpvars(0, nash_cipher_full_tb);
    end

endmodule