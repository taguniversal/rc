#!/usr/bin/env python3

import subprocess
import sys
import os
from pathlib import Path

def run_mkrand(format_type, num_vectors):
    """Run mkrand and parse its output"""
    try:
        result = subprocess.run(['mkrand', f'-f{format_type}', f'-n{num_vectors}'],
                              capture_output=True, text=True, check=True)
        return result.stdout.strip().split('\n')
    except subprocess.CalledProcessError as e:
        print(f"Error running mkrand: {e}", file=sys.stderr)
        sys.exit(1)

def format_binary_for_cryptol(binary_str):
    """Format binary string for Cryptol by taking first 32 bits"""
    bits = binary_str.replace(' ', '')[:32].ljust(32, '0')
    return f"[{bits}]"

def run_cryptol(commands):
    """Run Cryptol with given commands"""
    try:
        env = os.environ.copy()
        env['CRYPTOLPATH'] = 'src/cryptol'
        
        print("DEBUG: Running Cryptol with commands:")
        print(commands)
        
        # Combine all commands into a single string with semicolons
        command_str = '; '.join(cmd.strip() for cmd in commands.split('\n') if cmd.strip())
        
        proc = subprocess.Popen(['cryptol', '-c', command_str],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              text=True,
                              env=env,
                              cwd=os.getcwd())
        
        stdout, stderr = proc.communicate()
        
        print("\nDEBUG: Cryptol stdout:")
        print(stdout)
        print("\nDEBUG: Cryptol stderr:")
        print(stderr)
        
        if proc.returncode != 0:
            print(f"Cryptol error (return code {proc.returncode})")
            sys.exit(1)
        return stdout
    except Exception as e:
        print(f"Error running Cryptol: {str(e)}", file=sys.stderr)
        sys.exit(1)

def generate_cryptol_commands(key, input_bits):
    """Generate Cryptol commands for test vector generation"""
    formatted_input = format_binary_for_cryptol(input_bits)
    return f"""
:set path = [".", "src/cryptol"];
:set warnDefaulting = off;
:set warnShadowing = off;
:set show type errors;
:load TestVectors;
:set ascii = false;
:set base = 16;
let raw_key = "{key.strip('[]<>:')}"
:set base = 2;
let raw_input = {formatted_input};
let debug_vector = generate_test_output_debug (debug_parse_key raw_key) raw_input;
debug_vector
"""

def parse_cryptol_output(output, key, input_bits):
    """Parse Cryptol output to extract test vector data"""
    print("\nDEBUG: Parsing Cryptol output:")
    print(output)
    
    vector = {
        'key': key,
        'input_sequence': format_binary_for_cryptol(input_bits),
        'expected_output': None,
        'final_state': None
    }
    
    lines = [line.strip() for line in output.split('\n') if line.strip()]
    
    print("\nDEBUG: Processing lines:")
    for line in lines:
        print(f"Processing line: {line}")
        if 'expected_output' in line:
            try:
                vector['expected_output'] = line.split('=')[1].strip()
                print(f"Found expected output: {vector['expected_output']}")
            except:
                print(f"Warning: Could not parse output: {line}")
        elif 'final_state' in line:
            try:
                vector['final_state'] = line.split('=')[1].strip()
                print(f"Found final state: {vector['final_state']}")
            except:
                print(f"Warning: Could not parse state: {line}")
    
    print(f"\nDEBUG: Final vector: {vector}")
    return vector

def format_test_vector_file(vectors):
    """Format test vectors into NIST-style documentation"""
    output = []
    output.append("Nash Cipher Test Vectors")
    output.append("=======================\n")
    
    for i, vector in enumerate(vectors, 1):
        output.append(f"Test Vector {i}")
        output.append("-" * (12 + len(str(i))))
        output.append(f"Key: {vector.get('key', 'ERROR')}")
        output.append(f"Input Sequence: {vector.get('input_sequence', 'ERROR')}")
        output.append(f"Expected Output: {vector.get('expected_output', 'ERROR')}")
        output.append(f"Final State: {vector.get('final_state', 'ERROR')}\n")
    
    return "\n".join(output)

def main():
    if len(sys.argv) != 3:
        print("Usage: generate_vectors.py <num_vectors> <output_file>")
        sys.exit(1)
    
    num_vectors = int(sys.argv[1])
    output_file = Path(sys.argv[2])
    
    print("Generating keys with mkrand...")
    keys = run_mkrand(8, num_vectors)
    print(f"DEBUG: Generated keys: {keys}")
    
    print("\nGenerating input sequences with mkrand...")
    input_sequences = run_mkrand(2, num_vectors)
    print(f"DEBUG: Generated inputs: {input_sequences}")
    
    print("\nGenerating test vectors with Cryptol...")
    test_vectors = []
    for i, (key, input_seq) in enumerate(zip(keys, input_sequences), 1):
        print(f"\nProcessing vector {i}/{num_vectors}...")
        print(f"DEBUG: Using key: {key}")
        print(f"DEBUG: Using input: {input_seq}")
        print(f"DEBUG: Formatted input: {format_binary_for_cryptol(input_seq)}")
        
        cryptol_commands = generate_cryptol_commands(key, input_seq)
        cryptol_output = run_cryptol(cryptol_commands)
        vector = parse_cryptol_output(cryptol_output, key, input_seq)
        test_vectors.append(vector)
    
    print("\nFormatting output...")
    formatted_output = format_test_vector_file(test_vectors)
    output_file.write_text(formatted_output)
    print(f"\nGenerated test vectors written to {output_file}")

if __name__ == "__main__":
    main()