#include "iso_parser.h"
#include "iso_message.h"
#include "bitmap.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <sstream>
#include <map>

// Field definitions are now self-contained and drive both parsing and serialization
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

// --- Helper Functions ---
static std::string bcd_to_string(const std::vector<uint8_t>& bcd_data, size_t len) {
    std::stringstream ss;
    for (uint8_t byte : bcd_data) {
        ss << (int)((byte >> 4) & 0x0F); // High nibble
        ss << (int)(byte & 0x0F);      // Low nibble
    }
    std::string s = ss.str();
    return s.length() > len ? s.substr(0, len) : s;
}

// CORRECTED PARSING LOGIC
IsoMessage IsoParser::parse(const std::vector<uint8_t>& data) {
    initialize_field_defs();
    IsoMessage msg;
    size_t offset = 0;

    if (data.size() < 10) throw std::runtime_error("Data is too short for MTI and primary bitmap.");

    // MTI is 2-byte BCD
    std::stringstream mti_ss;
    mti_ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[0];
    mti_ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[1];
    msg.mti = mti_ss.str();
    offset = 2;

    Bitmap bitmap;
    int bitmap_bytes = ((data[offset] & 0x80) != 0) ? 16 : 8;
    if (data.size() < offset + bitmap_bytes) throw std::runtime_error("Data is too short for bitmap.");
    bitmap = Bitmap::from_bytes(std::vector<uint8_t>(data.begin() + offset, data.begin() + offset + bitmap_bytes));
    offset += bitmap_bytes;

    for (int i = 1; i <= 128; ++i) {
        if (bitmap.is_set(i)) {
            const auto& def_it = field_defs.find(i);
            if (def_it == field_defs.end()) {
                continue;
            }
            const auto& def = def_it->second;
            int len = 0;

            if (def.format == FieldFormat::LLVAR) {
                if (offset + 1 > data.size()) throw std::runtime_error("Buffer overflow reading LLVAR length for field " + to_string(i));
                len = ((data[offset] >> 4) * 10) + (data[offset] & 0x0F);
                offset += 1;
            } else { // FIXED
                len = def.max_length;
            }

            int bytes_to_read = (def.encoding == FieldEncoding::BCD) ? (len + 1) / 2 : len;
            if (offset + bytes_to_read > data.size()) throw std::runtime_error("Buffer overflow while parsing field " + to_string(i));

            std::vector<uint8_t> field_data(data.begin() + offset, data.begin() + offset + bytes_to_read);

            if (def.encoding == FieldEncoding::BCD) {
                msg.set_field(i, bcd_to_string(field_data, len));
            } else { // ASCII or BINARY
                msg.set_field(i, std::string(field_data.begin(), field_data.end()));
            }
            offset += bytes_to_read;
        }
    }
    return msg;
}