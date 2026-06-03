#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "types.h"
#include "arena_allocator.h"

using namespace std;

enum DistanceUnit { PC, LY, AU };

float convert_to_parsecs(float distance, DistanceUnit unit) {
    if (unit == LY) return distance * 0.306601f;
    if (unit == AU) return distance * 4.84814e-6f;
    return distance;
}

float convert_from_parsecs(float distance_pc, DistanceUnit unit) {
    if (unit == LY) return distance_pc / 0.306601f;
    if (unit == AU) return distance_pc / 4.84814e-6f;
    return distance_pc;
}

void print_star_metadata(ifstream& payload_file, uint64_t pointer, float dist_pc, DistanceUnit display_unit) {
    payload_file.seekg(pointer, ios::beg);
    string raw_csv;
    getline(payload_file, raw_csv, '\0');

    vector<string> fields;
    stringstream ss(raw_csv);
    string field;
    while (getline(ss, field, ',')) {
        fields.push_back(field);
    }

    // HYG mappings: ID=0, ProperName=6, Mag=13, Spectrum=15
    string name = fields.size() > 6 && !fields[6].empty() ? fields[6] : "Unnamed";
    string id = fields.size() > 0 ? fields[0] : "?";
    string mag = fields.size() > 13 ? fields[13] : "?";
    string spect = fields.size() > 15 ? fields[15] : "?";

    float converted_dist = convert_from_parsecs(dist_pc, display_unit);
    string unit_str = (display_unit == PC) ? " pc" : (display_unit == LY) ? " ly" : " au";

    cout << left << setw(10) << id << setw(20) << name << setw(15) << (to_string(converted_dist) + unit_str) 
         << setw(10) << mag << setw(15) << spect << endl;
}

float box_distance_squared(OctreeNode* node, float tx, float ty, float tz) {
    float closest_x = max(node->min_x, min(tx, node->max_x));
    float closest_y = max(node->min_y, min(ty, node->max_y));
    float closest_z = max(node->min_z, min(tz, node->max_z));

    float dx = closest_x - tx;
    float dy = closest_y - ty;
    float dz = closest_z - tz;
    return (dx * dx) + (dy * dy) + (dz * dz);
}

struct QueryResult {
    uint32_t star_idx;
    float dist_sq;
    bool operator<(const QueryResult& other) const {
        return dist_sq < other.dist_sq;
    }
};

void query_radius_recursive(OctreeNode* node, float tx, float ty, float tz, float r_sq, const Star* all_stars, vector<QueryResult>& results) {
    if (box_distance_squared(node, tx, ty, tz) > r_sq) return;

    if (node->is_leaf) {
        for (int i = 0; i < node->star_count; i++) {
            uint32_t s_idx = node->star_indices[i];
            const Star& s = all_stars[s_idx];
            float dx = s.x - tx; float dy = s.y - ty; float dz = s.z - tz;
            float dist_sq = (dx*dx) + (dy*dy) + (dz*dz);
            if (dist_sq <= r_sq) {
                results.push_back({s_idx, dist_sq});
            }
        }
    } else {
        for (int i = 0; i < 8; i++) {
            query_radius_recursive(&node->first_child[i], tx, ty, tz, r_sq, all_stars, results);
        }
    }
}

void query_knn_recursive(OctreeNode* node, float tx, float ty, float tz, int k, const Star* all_stars, priority_queue<QueryResult>& heap) {
    float current_r_sq = (heap.size() == k) ? heap.top().dist_sq : numeric_limits<float>::max();
    
    if (box_distance_squared(node, tx, ty, tz) > current_r_sq) return;

    if (node->is_leaf) {
        for (int i = 0; i < node->star_count; i++) {
            uint32_t s_idx = node->star_indices[i];
            const Star& s = all_stars[s_idx];
            float dx = s.x - tx; float dy = s.y - ty; float dz = s.z - tz;
            float dist_sq = (dx*dx) + (dy*dy) + (dz*dz);
            
            if (heap.size() < k) {
                heap.push({s_idx, dist_sq});
            } else if (dist_sq < heap.top().dist_sq) {
                heap.pop();
                heap.push({s_idx, dist_sq});
            }
        }
    } else {
        for (int i = 0; i < 8; i++) {
            query_knn_recursive(&node->first_child[i], tx, ty, tz, k, all_stars, heap);
        }
    }
}

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
    
    cout << "\nQuery Engine:" << endl;
    
    ifstream payload_file("./data/parsed/payload.bin", ios::binary);
    if (!payload_file.is_open()) {
        cerr << "CRITICAL ERROR: Could not open payload.bin for queries" << endl;
        return 1;
    }

    // Target: Earth (0, 0, 0)
    float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f;
    
    // Radius Search (5 Parsecs)
    float search_radius_pc = convert_to_parsecs(5.0f, PC);
    cout << "\n[Query] Radius Search: Stars within 5 Parsecs of Earth" << endl;
    cout << left << setw(10) << "ID" << setw(20) << "Name" << setw(15) << "Distance" << setw(10) << "Mag" << setw(15) << "Spectrum" << endl;
    cout << "----------------------------------------------------------------------" << endl;
    
    vector<QueryResult> r_results;
    auto q1_start = chrono::high_resolution_clock::now();
    query_radius_recursive(root, target_x, target_y, target_z, search_radius_pc * search_radius_pc, stars, r_results);
    auto q1_end = chrono::high_resolution_clock::now();
    
    // Sort results by distance
    sort(r_results.begin(), r_results.end(), [](const QueryResult& a, const QueryResult& b) { return a.dist_sq < b.dist_sq; });
    for (const auto& res : r_results) {
        print_star_metadata(payload_file, stars[res.star_idx].payload_pointer, sqrt(res.dist_sq), PC);
    }
    cout << "Radius Search executed in " << chrono::duration<double, milli>(q1_end - q1_start).count() << " ms." << endl;

    // Test 2: KNN Search (Closest 10 stars)
    cout << "\n[Query] KNN Search: 10 closest stars to Earth (Light Years)" << endl;
    cout << left << setw(10) << "ID" << setw(20) << "Name" << setw(15) << "Distance" << setw(10) << "Mag" << setw(15) << "Spectrum" << endl;
    cout << "----------------------------------------------------------------------" << endl;
    
    priority_queue<QueryResult> knn_heap;
    auto q2_start = chrono::high_resolution_clock::now();
    query_knn_recursive(root, target_x, target_y, target_z, 10, stars, knn_heap);
    auto q2_end = chrono::high_resolution_clock::now();

    // The heap pops the furthest first, so we extract and reverse to print closest first
    vector<QueryResult> knn_results;
    while (!knn_heap.empty()) {
        knn_results.push_back(knn_heap.top());
        knn_heap.pop();
    }
    reverse(knn_results.begin(), knn_results.end());

    for (const auto& res : knn_results) {
        print_star_metadata(payload_file, stars[res.star_idx].payload_pointer, sqrt(res.dist_sq), LY);
    }
    cout << "KNN Search executed in " << chrono::duration<double, milli>(q2_end - q2_start).count() << " ms." << endl;

    delete[] stars;
    return 0;
}
