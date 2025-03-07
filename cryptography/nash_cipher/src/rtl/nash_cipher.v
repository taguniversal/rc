module nash_permuter #(
    parameter STATE_WIDTH = 4,
    parameter MEM_DEPTH = 8
) (
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire                    input_bit,
    input  wire                    perm_select, // 0=blue, 1=red
    input  wire [MEM_DEPTH-1:0]    initial_mem,
    output wire                    output_bit,
    output wire                    valid
);

    // State registers
    reg [STATE_WIDTH-1:0] curr_state;
    reg [MEM_DEPTH-1:0]   memory;
    reg                   output_reg;
    reg                   valid_reg;

    // Permutation tables (will be initialized by higher level module)
    reg [STATE_WIDTH-1:0] red_next_state  [0:(1<<STATE_WIDTH)-1];
    reg                   red_transform   [0:(1<<STATE_WIDTH)-1];
    reg [STATE_WIDTH-1:0] blue_next_state [0:(1<<STATE_WIDTH)-1];
    reg                   blue_transform  [0:(1<<STATE_WIDTH)-1];

    // Combinational signals
    wire [STATE_WIDTH-1:0] next_state;
    wire                   transform_fn;
    wire                   transformed_bit;

    // Select permutation based on perm_select
    assign next_state = perm_select ? red_next_state[curr_state] 
                                  : blue_next_state[curr_state];
    assign transform_fn = perm_select ? red_transform[curr_state]
                                    : blue_transform[curr_state];

    // Apply transformation function
    assign transformed_bit = transform_fn ? ~input_bit : input_bit;

    // Sequential logic
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            curr_state <= 0;
            memory <= initial_mem;
            output_reg <= 0;
            valid_reg <= 0;
        end else begin
            curr_state <= next_state;
            memory <= {transformed_bit, memory[MEM_DEPTH-1:1]};
            output_reg <= memory[0];
            valid_reg <= 1;
        end
    end

    // Outputs
    assign output_bit = output_reg;
    assign valid = valid_reg;

endmodule

module nash_cipher_top (
    input  wire clk,
    input  wire rst_n,
    input  wire input_bit,
    input  wire [7:0] key_data,
    output wire cipher_bit,
    output wire valid
);

    // Internal signals
    wire permuter_out;
    wire feedback;

    // XOR for encryption
    assign cipher_bit = input_bit ^ permuter_out;
    assign feedback = cipher_bit;

    // Permuter instance
    nash_permuter permuter (
        .clk(clk),
        .rst_n(rst_n),
        .input_bit(feedback),
        .perm_select(feedback),  // Use cipher output to select permutation
        .initial_mem(key_data),
        .output_bit(permuter_out),
        .valid(valid)
    );

endmodule