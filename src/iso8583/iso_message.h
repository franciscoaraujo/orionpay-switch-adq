#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

// All field-related definitions are now centralized here.
enum class FieldFormat { FIXED, LLVAR, LLLVAR };
enum class FieldEncoding { BCD, ASCII, BINARY };

struct FieldDef {
    int id;
    int max_length;
    FieldFormat format;
    FieldEncoding encoding;
};

class IsoMessage {
public:
    std::string mti;
    std::map<int, std::string> fields;

    bool has_field(int field) const;
    std::string get_field(int field) const;
    void set_field(int field, const std::string& val);
    void set_mti(const std::string& new_mti);
    std::string get_mti() const;

    // Serialization and utilities
    std::vector<uint8_t> serialize() const;
    std::string mask_pan() const;
    std::string get_unique_key() const;
};
