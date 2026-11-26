#pragma once

#include "iso_message.h"
#include <string>
#include <map>

// This class is now a wrapper around the definitions in iso_message.h
class IsoFieldDef {
public:
    static const std::map<int, FieldDef>& get_definitions();
    static bool is_valid_for_mti(const std::string& mti, int field);
};
