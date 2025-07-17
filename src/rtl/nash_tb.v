`timescale 1ns / 1ps

module NashCipher_tb;

    reg clk;
    reg reset;
    reg [127:0] seed;
    reg [127:0] plaintext;
    wire [127:0] ciphertext;
    wire [127:0] decrypted;

    // Instantiate Transmitter
    NashTransmitter tx (
        .clk(clk),
        .reset(reset),
        .seed(seed),
        .plaintext(plaintext),
        .ciphertext(ciphertext)
    );

    // Instantiate Receiver
    NashReceiver rx (
        .clk(clk),
        .reset(reset),
        .seed(seed),
        .ciphertext(ciphertext),
        .plaintext(decrypted)
    );

    initial begin
        $dumpfile("nash_wave.vcd");
        $dumpvars(0, NashCipher_tb);

        clk = 0;
        reset = 1;
        seed = 128'hDEADBEEFCAFEBABE1234567890ABCDEF;
        plaintext = 128'hFACEFACEFACEFACEFACEFACEFACEFACE;

        #10 reset = 0;

        // Wait a few cycles
        #100;

        // Check result
        $display("Original plaintext: %h", plaintext);
        $display("Ciphertext:         %h", ciphertext);
        $display("Decrypted:          %h", decrypted);

        if (decrypted == plaintext) begin
            $display("✅ Decryption successful!");
        end else begin
            $display("❌ Decryption failed.");
        end

        $finish;
    end

    always #5 clk = ~clk;

endmodule
