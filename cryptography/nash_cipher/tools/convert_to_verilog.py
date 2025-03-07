import subprocess
import random


def get_entropy():
    """Runs mkrand -f2 -n2 and captures two sequences of 128 bits."""
    result = subprocess.run(["mkrand", "-f2", "-n2"], capture_output=True, text=True)
    lines = result.stdout.strip().split("\n")

    if len(lines) != 2 or len(lines[0]) != 128 or len(lines[1]) != 128:
        raise ValueError("Expected two 128-bit sequences from mkrand")

    return lines[0], lines[1]  # red, blue entropy sequences


def generate_permutation_table(entropy):
    """Generates a valid 128-state permutation table."""
    states = random.sample(range(128), 128)
    transformations = [int(b) for b in entropy]  # Convert entropy bits to integers (0/1)
    return list(zip(states, transformations))


def write_verilog_file(red_table, blue_table, filename="src/rtl/nash_permutation_tables.v"):
    """Writes the red and blue permutation tables to a Verilog module."""
    print("Generating Verilog permutation table with 128 entries per permuter...")

    with open(filename, "w") as f:
        f.write("module nash_permutation_tables (\n")
        f.write("    input wire [7:0] index,\n")
        f.write("    output reg [7:0] red_next_state,\n")
        f.write("    output reg red_transform,\n")
        f.write("    output reg [7:0] blue_next_state,\n")
        f.write("    output reg blue_transform\n")
        f.write(");\n\n")

        # Red permutation LUT
        f.write("    always @* begin\n")
        f.write("        case (index)\n")
        for i, (next_state, trans) in enumerate(red_table):
            f.write(f"            8'd{i}: begin red_next_state = 8'd{next_state}; red_transform = 1'b{trans}; end\n")
        f.write("            default: begin red_next_state = 8'd0; red_transform = 1'b0; end\n")
        f.write("        endcase\n")
        f.write("    end\n\n")

        # Blue permutation LUT
        f.write("    always @* begin\n")
        f.write("        case (index)\n")
        for i, (next_state, trans) in enumerate(blue_table):
            f.write(f"            8'd{i}: begin blue_next_state = 8'd{next_state}; blue_transform = 1'b{trans}; end\n")
        f.write("            default: begin blue_next_state = 8'd0; blue_transform = 1'b0; end\n")
        f.write("        endcase\n")
        f.write("    end\n")

        f.write("endmodule\n")

    print(f"Generated {filename} with 128 entries per permuter.")


# Get entropy for both red and blue permutations
red_entropy, blue_entropy = get_entropy()

# Generate full 128-entry permutation tables
red_table = generate_permutation_table(red_entropy)
blue_table = generate_permutation_table(blue_entropy)

# Write Verilog file
write_verilog_file(red_table, blue_table)
