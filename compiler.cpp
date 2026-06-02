#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include "types.h"

using namespace std;

string FILE_NAME = "./data/hyg_v38.csv";

vector<string> parse_csv_line(const string& line) {
    vector<string> fields;
    string current_field;
    bool inside_quotes = false;
    
    for (char c : line) {
        if (c == '"') {
            inside_quotes = !inside_quotes;
        } else if (c == ',' && !inside_quotes) {
            fields.push_back(current_field);
            current_field.clear();
        } else {
            current_field += c;
        }
    }
    fields.push_back(current_field);
    return fields;
}

int main() {
    cout << "Binary Database Compiler" << endl;
    
    ifstream csv_file(FILE_NAME);
    if (!csv_file.is_open()) {
        cerr << "CRITICAL ERROR: Could not open " << FILE_NAME << endl;
        return 1;
    }
    
    ofstream geo_file("./data/parsed/geometry.bin", ios::binary);
    ofstream payload_file("./data/parsed/payload.bin", ios::binary);
    
    if (!geo_file.is_open() || !payload_file.is_open()) {
        cerr << "CRITICAL ERROR: Could not create output binary files." << endl;
        return 1;
    }

    string line;
    getline(csv_file, line);
    
    uint64_t current_payload_offset = 0;
    size_t processed_count = 0;
    size_t error_count = 0;

    cout << "Compiling dataset into geometry.bin and payload.bin..." << endl;

    auto start_time = chrono::high_resolution_clock::now();

    while (getline(csv_file, line)) {
        if (line.empty()) continue;
        
        vector<string> fields = parse_csv_line(line);
        
        // HYG v38 mapping: x=17, y=18, z=19
        if (fields.size() < 20) {
            error_count++;
            continue;
        }

        // Building geometry string
        Star s;
        try {
            s.x = fields[17].empty() ? 0.0f : stof(fields[17]);
            s.y = fields[18].empty() ? 0.0f : stof(fields[18]);
            s.z = fields[19].empty() ? 0.0f : stof(fields[19]);
        } catch (...) {
            error_count++;
            continue;
        }
        
        // Building payload string
        s.payload_pointer = current_payload_offset;
        string metadata_str;
        for (size_t i = 0; i < fields.size(); i++) {
            if (i == 17 || i == 18 || i == 19) continue;
            if (!metadata_str.empty()) metadata_str += ",";
            metadata_str += fields[i];
        }

        geo_file.write(reinterpret_cast<const char*>(&s), sizeof(Star));
        
        payload_file.write(metadata_str.c_str(), metadata_str.size());
        char null_term = '\0';
        payload_file.write(&null_term, 1);
        
        current_payload_offset += metadata_str.size() + 1;
        processed_count++;
    }

    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> diff = end_time - start_time;

    geo_file.close();
    payload_file.close();
    csv_file.close();

    cout << "\n[Compilation Complete]" << endl;
    cout << "Total Stars Compiled: " << processed_count << endl;
    cout << "Format Errors Skipped: " << error_count << endl;
    cout << "Final Payload Size: " << current_payload_offset << " bytes" << endl;
    cout << "Processing Time: " << diff.count() << " seconds" << endl;
    
    return 0;
}
