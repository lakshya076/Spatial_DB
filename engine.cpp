#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <limits>
#include "types.h"
#include "arena_allocator.h"

using namespace std;

void set_child_bounds(OctreeNode* parent, OctreeNode* child, int index) {
    float mid_x = (parent->min_x + parent->max_x) / 2.0f;
    float mid_y = (parent->min_y + parent->max_y) / 2.0f;
    float mid_z = (parent->min_z + parent->max_z) / 2.0f;

    child->min_x = (index & 1) ? mid_x : parent->min_x;
    child->max_x = (index & 1) ? parent->max_x : mid_x;

    child->min_y = (index & 2) ? mid_y : parent->min_y;
    child->max_y = (index & 2) ? parent->max_y : mid_y;

    child->min_z = (index & 4) ? mid_z : parent->min_z;
    child->max_z = (index & 4) ? parent->max_z : mid_z;

    child->is_leaf = true;
    child->star_count = 0;
}

int get_child_index(OctreeNode* node, float x, float y, float z) {
    float mid_x = (node->min_x + node->max_x) / 2.0f;
    float mid_y = (node->min_y + node->max_y) / 2.0f;
    float mid_z = (node->min_z + node->max_z) / 2.0f;

    int idx = 0;
    idx |= (x >= mid_x) ? 1 : 0;
    idx |= (y >= mid_y) ? 2 : 0;
    idx |= (z >= mid_z) ? 4 : 0;
    return idx;
}

void insert_star(OctreeNode* node, uint32_t star_idx, const Star* all_stars, ArenaAllocator& arena) {
    if (node->is_leaf) {
        if (node->star_count < 8) {
            node->star_indices[node->star_count++] = star_idx;
        } else {
            node->is_leaf = false;
            
            uint32_t old_stars[8];
            for (int i = 0; i < 8; i++) {
                old_stars[i] = node->star_indices[i];
            }

            // Allocate 512 bytes for 8 contiguous children
            OctreeNode* children = arena.alloc_array<OctreeNode>(8);
            node->first_child = children;

            for (int i = 0; i < 8; i++) {
                set_child_bounds(node, &children[i], i);
            }

            // Route the original 8 stars into new children
            for (int i = 0; i < 8; i++) {
                uint32_t s_idx = old_stars[i];
                int c_idx = get_child_index(node, all_stars[s_idx].x, all_stars[s_idx].y, all_stars[s_idx].z);
                insert_star(&children[c_idx], s_idx, all_stars, arena);
            }

            // Route the new star
            int c_idx = get_child_index(node, all_stars[star_idx].x, all_stars[star_idx].y, all_stars[star_idx].z);
            insert_star(&children[c_idx], star_idx, all_stars, arena);
        }
    } else {
        int c_idx = get_child_index(node, all_stars[star_idx].x, all_stars[star_idx].y, all_stars[star_idx].z);
        insert_star(&node->first_child[c_idx], star_idx, all_stars, arena);
    }
}

int main() {    
    ifstream geo_file("./data/parsed/geometry.bin", ios::binary | ios::ate);
    if (!geo_file.is_open()) {
        cerr << "CRITICAL ERROR: Could not open geometry.bin" << endl;
        return 1;
    }

    size_t file_size = geo_file.tellg();
    geo_file.seekg(0, ios::beg);
    
    size_t num_stars = file_size / sizeof(Star);
    cout << "Loading " << num_stars << " stars into RAM..." << endl;

    auto start_time = chrono::high_resolution_clock::now();

    Star* stars = new Star[num_stars];
    geo_file.read(reinterpret_cast<char*>(stars), file_size);
    geo_file.close();
    
    float min_x = numeric_limits<float>::max(), min_y = numeric_limits<float>::max(), min_z = numeric_limits<float>::max();
    float max_x = numeric_limits<float>::lowest(), max_y = numeric_limits<float>::lowest(), max_z = numeric_limits<float>::lowest();

    for (size_t i = 0; i < num_stars; i++) {
        if (stars[i].x < min_x) min_x = stars[i].x;
        if (stars[i].y < min_y) min_y = stars[i].y;
        if (stars[i].z < min_z) min_z = stars[i].z;
        if (stars[i].x > max_x) max_x = stars[i].x;
        if (stars[i].y > max_y) max_y = stars[i].y;
        if (stars[i].z > max_z) max_z = stars[i].z;
    }

    ArenaAllocator arena(1024 * 1024 * 64);
    OctreeNode* root = arena.alloc<OctreeNode>();
    
    root->min_x = min_x; root->max_x = max_x;
    root->min_y = min_y; root->max_y = max_y;
    root->min_z = min_z; root->max_z = max_z;
    root->is_leaf = true;
    root->star_count = 0;

    cout << "Building Octree..." << endl;
    for (uint32_t i = 0; i < num_stars; i++) {
        insert_star(root, i, stars, arena);
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> diff = end_time - start_time;

    cout << "\nTotal Initialization Time: " << diff.count() << " seconds" << endl;
    cout << "Arena Memory Consumed: " << (arena.get_used_memory() / 1024.0 / 1024.0) << " MB" << endl;
    
    delete[] stars;
    return 0;
}
