#!/bin/bash
# save_diagrams.sh

# Get the project root directory (assuming script is in tools/)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Create directories if they don't exist
mkdir -p "${PROJECT_ROOT}/docs/diagrams"
mkdir -p "${PROJECT_ROOT}/docs/nist/figures"

# Save permuter diagram
cat > "${PROJECT_ROOT}/docs/diagrams/permuter.dot" << 'EOF'
digraph NashPermuter {
    // Graph settings
    rankdir=LR;
    node [shape=circle, fontname="Arial"];
    edge [fontname="Arial"];

    // States
    node [fixedsize=true, width=0.6]
    1 [label="1"]
    2 [label="2"]
    3 [label="3"]
    4 [label="4"]
    5 [label="5"]
    6 [label="6"]
    7 [label="7"]
    8 [label="8"]

    // Edges for red permutation (using solid lines)
    1 -> 4 [color="red", label="+"];
    4 -> 7 [color="red", label="-"];
    7 -> 5 [color="red", label="+"];
    5 -> 2 [color="red", label="+"];
    2 -> 3 [color="red", label="+"];
    3 -> 6 [color="red", label="-"];
    6 -> 8 [color="red", label="-"];

    // Edges for blue permutation (using dashed lines)
    1 -> 3 [color="blue", style=dashed, label="-"];
    3 -> 4 [color="blue", style=dashed, label="-"];
    4 -> 6 [color="blue", style=dashed, label="+"];
    6 -> 7 [color="blue", style=dashed, label="+"];
    7 -> 2 [color="blue", style=dashed, label="-"];
    2 -> 5 [color="blue", style=dashed, label="-"];
    5 -> 8 [color="blue", style=dashed, label="+"];

    // Legend
    subgraph cluster_legend {
        label="Legend";
        node [shape=plaintext];
        edge [len=1.5];
        l1 [label="Red Path"];
        l2 [label="Blue Path"];
        l3 [label="+ = No Change"];
        l4 [label="- = Invert"];
    }
}
EOF

# Save transmitter diagram
cat > "${PROJECT_ROOT}/docs/diagrams/transmitter.dot" << 'EOF'
digraph NashTransmitter {
    // Graph settings
    rankdir=LR;
    node [shape=box, fontname="Arial"];
    edge [fontname="Arial"];

    // Input and Output
    input [shape=none, label="Input"];
    output [shape=none, label="Output"];

    // Components
    A [label="A\n(Adder)"];
    D [label="D\n(Decider)"];
    P [label="P\n(Permuter)"];

    // Connections
    input -> A;
    A -> output;
    A -> D;
    D -> P;
    P -> A [constraint=false];

    // Labels
    subgraph cluster_legend {
        label="Legend";
        node [shape=plaintext];
        l1 [label="A: Binary Addition mod 2"];
        l2 [label="D: Chooses Red/Blue Path"];
        l3 [label="P: Permuter"];
    }
}
EOF

# Save receiver diagram
cat > "${PROJECT_ROOT}/docs/diagrams/receiver.dot" << 'EOF'
digraph NashReceiver {
    // Graph settings
    rankdir=LR;
    node [shape=box, fontname="Arial"];
    edge [fontname="Arial"];

    // Input and Output
    input [shape=none, label="Input\n(Ciphertext)"];
    output [shape=none, label="Output\n(Plaintext)"];

    // Components
    R [label="R\n(Retarder)"];
    A [label="A\n(Adder)"];
    D [label="D\n(Decider)"];
    P [label="P\n(Permuter)"];

    // Split point for input
    split [shape=point];

    // Connections following Nash's original diagram
    input -> split;
    split -> A;              // Direct path to adder
    split -> R;              // Path through retarder
    R -> D;                  // Retarder to decider
    D -> P;                  // Decider controls permuter
    P -> A;                  // Permuter output to adder
    A -> output;             // Adder output is plaintext

    // Labels
    subgraph cluster_legend {
        label="Legend";
        node [shape=plaintext];
        l1 [label="R: One-Unit Delay"];
        l2 [label="A: Binary Addition mod 2"];
        l3 [label="D: Chooses Red/Blue Path"];
        l4 [label="P: Permuter"];
    }
}
EOF

echo "DOT files created in docs/diagrams/"