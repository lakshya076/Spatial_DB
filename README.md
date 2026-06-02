# Spatial DB

A highly optimized, hardware-cache-aligned 3D Spatial Database built in pure C++ (no external dependencies) specifically designed to ingest and route astronomical star datasets into a Barnes-Hut Galaxy Simulator style Octree.

## Overview
This project uses a split-binary architecture to achieve massive performance:
1. **Geometry Data:** Tightly packed, 64-byte aligned structs are loaded directly into an `ArenaAllocator` for hardware L1 cache optimization.
2. **Payload Data:** Heavy metadata (strings, text, star names) are completely decoupled from the math nodes to avoid CPU cache pollution, accessed via a 64-bit pointer.

### Dataset Acknowledgement
The default star dataset used for testing is the HYG Database. A massive thank you to David Nash for providing it.
You can find the raw HYG dataset here: [astronexus/hyg-database](https://github.com/astronexus/hyg-database)

## Project Structure
```text
.
├── .gitignore
├── README.md
├── types.h               # Core data structures (Star, OctreeNode) strictly aligned to 64 bytes
├── arena_allocator.h     # Custom memory arena allocator using placement new
├── compiler.cpp          # The offline tool to parse CSVs into the split-binary format
└── data/                 # Ignored in git. Place your raw CSVs and generated binaries here.
    ├── hyg_v38.csv       # Raw astronomy dataset (not included)
    └── parsed/           # Output directory for geometry.bin and payload.bin
```

## How to Run

### 1. Setup the Data
First, download the `hyg_v38.csv` file from the [HYG Database repository](https://github.com/astronexus/hyg-database) and place it inside the `data/` directory.

Ensure the output directory exists:
```bash
mkdir -p data/parsed
```

### 2. Compile the Database Compiler
You must have a compiler supporting C++17. Compile the `compiler.cpp` offline tool with `-O3` optimization for maximum parsing speed:

```bash
g++ -std=c++17 -O3 compiler.cpp -o compiler.exe
```

### 3. Generate the Binary Databases
Run the compiler tool to ingest the CSV and split the data into `geometry.bin` and `payload.bin`. 
```bash
./compiler.exe
```
You will now see the optimized binary files inside `data/parsed/`.
