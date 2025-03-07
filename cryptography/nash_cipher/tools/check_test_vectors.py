import re

# Path to RandData.cry
RANDDATA_FILE = "src/cryptol/RandData.cry"

def parse_next_states(filename):
    """Extracts next_state values from RandData.cry and stores them as integers."""
    next_states_red = {}
    next_states_blue = {}
    
    with open(filename, "r") as f:
        content = f.readlines()

    # Regular expression to match next_state values
    state_pattern = re.compile(r"next_state\s*=\s*0b([01]+)")

    # Identify whether we are in red_perms or blue_perms
    current_dict = None
    state_counter = 0  # Track state number in decimal

    for line in content:
        line = line.strip()
        
        if "red_perms" in line:
            current_dict = next_states_red
            state_counter = 0
        elif "blue_perms" in line:
            current_dict = next_states_blue
            state_counter = 0

        match = state_pattern.search(line)
        if match and current_dict is not None:
            binary_value = match.group(1)
            state_value = int(binary_value, 2)  # Convert binary to integer
            current_dict[state_counter] = state_value  # Store as decimal
            state_counter += 1

    return next_states_red, next_states_blue

def print_state_mappings(next_states, label):
    """Prints each state's next state in decimal format."""
    print(f"🔍 {label} Permutation Table (Decimal):")
    for state, next_state in sorted(next_states.items()):
        print(f"  State {state:3d} → Next State {next_state:3d}")
    print()

def main():
    print("🔎 Verifying RandData.cry permutation tables...\n")
    red_states, blue_states = parse_next_states(RANDDATA_FILE)

    print_state_mappings(red_states, "Red Perms")
    print_state_mappings(blue_states, "Blue Perms")

if __name__ == "__main__":
    main()
