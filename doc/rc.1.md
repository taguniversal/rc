% RC(1) rc User Manual
% CryptoLogic LLC
% March 2025

# NAME
rc - Reality Compiler

# SYNOPSIS
**rc** [*options*] *input_file*

# DESCRIPTION
The **Reality Compiler (rc)** is a next-generation universal synthesis engine.  
It takes **Invocation Scripts** and generates **digital circuits**,  
**SPIR-V GPU binaries**, **KiCad PCB layouts**, and **3D Collada models**.

It is part of the **CryptoLogic toolchain**, enabling **hardware, software, and physical synthesis**  
from a single unified source.

# OPTIONS
**--help**  
: Show this help message.

**--version**  
: Display version information.

**--output** *file*  
: Specify the output filename.

# EXAMPLES
Compile an **Invocation Script** into a **SPIR-V binary**:
```sh
rc input/full_add.inv --output output.spv
```

Generate a **KiCad PCB layout**:
```sh
rc netlist/example.json --output output.kicad_pcb
```

Export a **3D Collada model**:
```sh
rc netlist/example.json --output output.dae
```

# AUTHOR
CryptoLogic LLC

# BUGS
Bugs? No.  
Unexpected emergent behavior? Maybe.

# SEE ALSO
**meson(1)**, **ninja(1)**, **spirv-val(1)**, **pdflatex(1)**, **pandoc(1)**
