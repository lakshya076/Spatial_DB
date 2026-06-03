# Cache-Aligned 3D Spatial Database

A high-performance, zero-dependency, cache-aligned 3D Spatial Database built from scratch in pure C++17. Designed to parse, index, and query massive astronomical star datasets (such as the HYG Database) using a custom memory arena-backed Octree.

> [!TIP]
> **Performance at a Glance:**
> * Ingests **119,626 stars** and constructs the entire Octree in **~18.8 ms**.
> * Memory Footprint: Only **~3.75 MB** of contiguous Arena space.
> * Radius Search (e.g., 5 Parsecs): **0.00 ms** (instantaneous).
> * KNN Search (e.g., 10 closest stars): **~0.52 ms** (instantaneous).

---

## Core Architecture

The database is built from the ground up for maximum cache locality and CPU execution efficiency.


### 1. Split-Binary Schema
To maximize CPU L1/L2 cache hits during spatial calculations, spatial coordinates are completely decoupled from heavy metadata:
* **Geometry Store ([geometry.bin](data/parsed/geometry.bin)):** Consists of tightly-packed 24-byte [Star](types.h#L5-L10) structs containing the `x`, `y`, `z` coordinates (floats) and a 64-bit file offset pointer to the metadata.
* **Payload Store ([payload.bin](data/parsed/payload.bin)):** Stores heavy, variable-length CSV fields (names, magnitudes, spectral types) as null-terminated strings. The database engine never loads this metadata into memory during spatial tree traversals, preventing CPU cache pollution.

### 2. Cache-Aligned Octree Node
Each node in the Octree is represented by the [OctreeNode](types.h#L13-L26) struct, which is designed to fit exactly on a single CPU cache line:
* **Strict Alignment:** Aligned to 64 bytes (`alignas(64)`) to prevent false sharing and ensure a node fits exactly within a standard L1 cache line.
* **Union-Based Layout:**
  - **Leaf Nodes:** Store up to 8 star indices (`uint32_t star_indices[8]`) pointing into the loaded star array.
  - **Internal Nodes:** Store a single pointer (`OctreeNode* first_child`) pointing to a contiguous block of 8 child nodes.
* **Size Guarantee:** A compile-time `static_assert` guarantees `sizeof(OctreeNode) == 64`.

### 3. Custom Memory Arena Allocator
Standard heap allocations (`new`/`malloc`) cause memory fragmentation and introduce high overhead. The database uses a custom [ArenaAllocator](arena_allocator.h):
* Allocates a single contiguous block of memory at startup (64 MB by default).
* Serves allocations via fast pointer-bumps.
* Allocates the 8 child nodes of a split parent contiguously in memory (`arena.alloc_array<OctreeNode>(8)`), guaranteeing that sibling nodes lie on the same or adjacent memory blocks.

---

## How it Works

### 1. Bitwise Index Routing
When inserting a star into a node, we determine which of the 8 child octants it belongs to by comparing its position $(x, y, z)$ against the node's midpoint $(m_x, m_y, m_z)$. We construct a 3-bit index in constant time using bitwise operations:
* Bit 0 (1): Set if $x \ge m_x$
* Bit 1 (2): Set if $y \ge m_y$
* Bit 2 (4): Set if $z \ge m_z$

```cpp
int idx = 0;
idx |= (x >= mid_x) ? 1 : 0;
idx |= (y >= mid_y) ? 2 : 0;
idx |= (z >= mid_z) ? 4 : 0;
```
This maps the star directly to child index `0` through `7` without branching pipelines.

### 2. Bounding Box Pruning Math
To avoid searching subtrees that cannot possibly contain matching stars, we calculate the squared distance from the search coordinate $(t_x, t_y, t_z)$ to the closest point on the node's Axis-Aligned Bounding Box (AABB):

$$d^2 = \sum_{i \in \{x,y,z\}} (\max(\text{min}_i, \min(t_i, \text{max}_i)) - t_i)^2$$

In C++ (implemented as `box_distance_squared` in [engine.cpp](engine.cpp#L55-L64)):
```cpp
float box_distance_squared(OctreeNode* node, float tx, float ty, float tz) {
    float closest_x = max(node->min_x, min(tx, node->max_x));
    float closest_y = max(node->min_y, min(ty, node->max_y));
    float closest_z = max(node->min_z, min(tz, node->max_z));

    float dx = closest_x - tx;
    float dy = closest_y - ty;
    float dz = closest_z - tz;
    return (dx * dx) + (dy * dy) + (dz * dz);
}
```

### 3. Spatial Search Algorithms

#### Radius Search (`query_radius_recursive`)
* Computes the bounding box distance $d^2$ for the current node.
* **Pruning Check:** If `distance_squared > (radius * radius)`, the search sphere completely misses this node's Axis-Aligned Bounding Box (AABB). The entire subtree is pruned instantly.
* If it is a leaf node, checks exact Euclidean distance for each star and registers matching stars.
* Recursively searches non-pruned child nodes.

#### K-Nearest Neighbors (`query_knn_recursive`)
* Maintains a Max-Heap (`std::priority_queue`) of the $K$ closest stars found so far.
* The current pruning threshold is dynamically defined:
  - If the heap has fewer than $K$ elements, the search radius is infinite.
  - If the heap has exactly $K$ elements, the search radius is set to the distance of the furthest star currently in the heap ($\sqrt{\text{heap.top().dist\_sq}}$).
* If a node's bounding box distance to the target is larger than the current heap's maximum distance, the node and all its children are pruned.
* As closer stars are found, the search radius shrinks, making subsequent subtrees prune even faster.

---

## 📏 Unit Conversion System

Astronomy datasets natively store positions in Parsecs ($dist$ column in the HYG Database). However, coordinates can be queried or displayed in multiple formats:

| Unit | Symbol | Parsec Conversion Factor | Description |
|---|---|---|---|
| **Parsec** | `PC` | `1.0` | Base internal unit of the database. |
| **Light Year** | `LY` | `0.306601` | $1\text{ pc} \approx 3.26156\text{ ly}$ |
| **Astronomical Unit** | `AU` | `4.84814e-6` | $1\text{ pc} \approx 206,265\text{ au}$ |

Queries automatically perform standard scale conversions for user convenience while maintaining maximum floating-point precision internally.

---

## 📂 Project Structure

* [types.h](types.h): Core database structs ([Star](types.h#L5) and [OctreeNode](types.h#L13)) strictly aligned for cache performance.
* [arena_allocator.h](arena_allocator.h): High-performance pointer-bump memory allocator.
* [compiler.cpp](compiler.cpp): Offline data ingest tool that splits CSV data into parsed binaries.
* [engine.cpp](engine.cpp): The database server/engine that builds the Octree in RAM and executes queries.
* `data/`: Local folder for input files (ignored by Git, except directory structure).
  * `hyg_v38.csv`: Raw database file.
  * `parsed/`: Folder containing output `geometry.bin` and `payload.bin`.

---

## 🛠️ Getting Started

### 1. Download the Dataset
Download the `hyg_v38.csv.gz` dataset from the [astronexus/hyg-database](https://github.com/astronexus/hyg-database) repository. Unzip and place it inside the `data/` folder as `data/hyg_v38.csv`.

### 2. Prepare the Directories
Create the parsed output folder:
```bash
mkdir -p data/parsed
```

### 3. Compile the Database Compiler & Engine
Compile the codes with `-O3` optimizations using a C++17 compliant compiler (such as GCC/MinGW):
```bash
g++ -std=c++17 -O3 compiler.cpp -o compiler.exe
g++ -std=c++17 -O3 engine.cpp -o engine.exe
```

### 4. Run the Binary Compilation
Execute the compiler to convert the raw CSV to binary:
```bash
./compiler.exe
```
*This parses the 119k lines and produces `data/parsed/geometry.bin` and `data/parsed/payload.bin`.*

### 5. Run the Spatial Database Engine
Execute the engine to load the data, construct the Octree, and run demo queries:
```bash
./engine.exe
```

---

## 🖥️ Demo Console Output

```text
Loading 119626 stars into RAM...
Building Octree...

Total Initialization Time: 0.018804 seconds
Arena Memory Consumed: 3.74765 MB

Query Engine:

[Query] Radius Search: Stars within 5 Parsecs of Earth
ID        Name                Distance       Mag       Spectrum       
----------------------------------------------------------------------
0         Sol                 1.031323 au    -26.7     G2V            
70666     Proxima Centauri    267268.812500 au11.01     M5Ve           
71453     Toliman             273210.968750 au1.35      K1V            
71456     Rigil Kentaurus     273211.000000 au-0.01     G2V            
87665     Barnard's Star      375980.500000 au9.54      sdM4           
118720    Wolf 359            493233.906250 au13.45     M6             
53879     Lalande 21185       525180.000000 au7.49      M2V            
118079    Unnamed             542101.500000 au12.57     dM5.5e         
...
Radius Search executed in 0 ms.

[Query] KNN Search: 10 closest stars to Earth (Light Years)
ID        Name                Distance       Mag       Spectrum       
----------------------------------------------------------------------
0         Sol                 0.000016 ly    -26.7     G2V            
70666     Proxima Centauri    4.226199 ly    11.01     M5Ve           
71453     Toliman             4.320159 ly    1.35      K1V            
71456     Rigil Kentaurus     4.320159 ly    -0.01     G2V            
87665     Barnard's Star      5.945206 ly    9.54      sdM4           
118720    Wolf 359            7.799280 ly    13.45     M6             
53879     Lalande 21185       8.304429 ly    7.49      M2V            
118079    Unnamed             8.572001 ly    12.57     dM5.5e         
118080    Unnamed             8.572500 ly    12.7      dM5.5e         
32263     Sirius              8.600806 ly    -1.44     A0m...         
KNN Search executed in 0.525 ms.
```
