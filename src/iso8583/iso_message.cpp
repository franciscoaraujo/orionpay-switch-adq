#include "iso_message.h"
#include "bitmap.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <map>
#include <stdexcept>

// Field definitions to drive serialization and parsing
static std::map<int, FieldDef> field_defs;

static void initialize_field_defs() {
    if (!field_defs.empty()) return;
    // Based on the user's requirement document
    field_defs[2]   = {2,  19, FieldFormat::LLVAR, FieldEncoding::BCD};
    field_defs[4]   = {4,  12, FieldFormat::FIXED, FieldEncoding::BCD};
    field_defs[11]  = {11, 6,  FieldFormat::FIXED, FieldEncoding::BCD};
    field_defs[37]  = {37, 12, FieldFormat::FIXED, FieldEncoding::ASCII};
    field_defs[38]  = {38, 6,  FieldFormat::FIXED, FieldEncoding::ASCII};
    field_defs[39]  = {39, 2,  FieldFormat::FIXED, FieldEncoding::ASCII};
    field_defs[41]  = {41, 8,  FieldFormat::FIXED, FieldEncoding::ASCII};
    field_defs[49]  = {49, 3,  FieldFormat::FIXED, FieldEncoding::BCD};
    field_defs[52]  = {52, 16, FieldFormat::FIXED, FieldEncoding::BINARY};
}


bool IsoMessage::has_field(int field) const {
    return fields.find(field) != fields.end();
}

std::string IsoMessage::get_field(int field) const {
    auto it = fields.find(field);
    return it != fields.end() ? it->second : "";
}

void IsoMessage::set_field(int field, const std::string& val) {
    fields[field] = val;
}

void IsoMessage::set_mti(const std::string& new_mti) {
    mti = new_mti;
}

std::string IsoMessage::get_mti() const {
    return mti;
}

std::vector<uint8_t> IsoMessage::serialize() const {
    initialize_field_defs();
    std::vector<uint8_t> data;
    
    // MTI: Send as 2-byte BCD
    if (mti.length() == 4) {
        uint8_t byte1 = (mti[0] - '0') << 4 | (mti[1] - '0');
        uint8_t byte2 = (mti[2] - '0') << 4 | (mti[3] - '0');
        data.push_back(byte1);
        data.push_back(byte2);
    } else {
        data.push_back(0x00);
        data.push_back(0x00);
    }

    // Bitmap
    Bitmap bitmap;
    // Use a sorted map to ensure fields are processed in order
    std::map<int, std::string> sorted_fields(fields.begin(), fields.end());
    for (const auto& pair : sorted_fields) {
        bitmap.set(pair.first);
    }
    auto bitmap_bytes = bitmap.to_bytes();
    data.insert(data.end(), bitmap_bytes.begin(), bitmap_bytes.end());

    // Helper: encode numeric string into BCD (nibbles)
    auto encode_bcd = [](const std::string& digits, std::vector<uint8_t>& out) {
        std::string s = digits;
        if (s.length() % 2 != 0) {
            s = "0" + s;
        }
        for (size_t i = 0; i < s.length(); i += 2) {
            uint8_t hi = (s[i] - '0');
            uint8_t lo = (s[i+1] - '0');
            out.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
    };

    // Serialize fields based on the sorted map
    for (const auto& pair : sorted_fields) {
        int field_num = pair.first;
        const auto& value = pair.second;

        const auto& def_it = field_defs.find(field_num);
        if (def_it == field_defs.end()) {
            continue;
        }
        const auto& def = def_it->second;

        // Add length prefix if variable (as 1-byte BCD)
        if (def.format == FieldFormat::LLVAR) {
            int len = static_cast<int>(value.length());
            if (len > 99) len = 99;
            data.push_back(static_cast<uint8_t>(((len / 10) << 4) | (len % 10)));
        }

        // Add data, encoded correctly
        if (def.encoding == FieldEncoding::BCD) {
            encode_bcd(value, data);
        } else { // ASCII or BINARY
            data.insert(data.end(), value.begin(), value.end());
        }
    }
    return data;
}

std::string IsoMessage::mask_pan() const {
    std::string pan = get_field(2);
    if (pan.length() > 10) {
        return pan.substr(0, 6) + std::string(pan.length() - 10, '*') + pan.substr(pan.length() - 4);
    }
    return pan;
}

std::string IsoMessage::get_unique_key() const {
    return get_field(11) + get_field(41);
}