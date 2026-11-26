#pragma once

#include "iso_message.h"
#include <vector>
#include <cstdint>
#include <map>
#include <string>

class IsoParser {
public:
    static IsoMessage parse(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> build(const IsoMessage& msg);
};
