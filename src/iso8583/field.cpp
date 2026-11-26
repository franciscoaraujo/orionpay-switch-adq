#include "field.h"

using namespace std;

const map<int, FieldDef>& IsoFieldDef::get_definitions() {
    static const map<int, FieldDef> definitions = {
        {2,  {2, 19, FieldFormat::LLVAR, FieldEncoding::BCD}},
        {3,  {3, 6, FieldFormat::FIXED, FieldEncoding::BCD}},
        {4,  {4, 12, FieldFormat::FIXED, FieldEncoding::BCD}},
        {7,  {7, 10, FieldFormat::FIXED, FieldEncoding::BCD}},
        {11, {11, 6, FieldFormat::FIXED, FieldEncoding::BCD}},
        {12, {12, 6, FieldFormat::FIXED, FieldEncoding::BCD}},
        {13, {13, 4, FieldFormat::FIXED, FieldEncoding::BCD}},
        {22, {22, 3, FieldFormat::FIXED, FieldEncoding::BCD}},
        {39, {39, 2, FieldFormat::FIXED, FieldEncoding::ASCII}},
        {41, {41, 8, FieldFormat::FIXED, FieldEncoding::ASCII}},
        {42, {42, 15, FieldFormat::FIXED, FieldEncoding::ASCII}},
        {49, {49, 3, FieldFormat::FIXED, FieldEncoding::ASCII}},
        {52, {52, 8, FieldFormat::FIXED, FieldEncoding::BINARY}}
    };
    return definitions;
}

bool IsoFieldDef::is_valid_for_mti(const string& mti, int field) {
    if (mti == "0110" && field == 39) return true;
    if (mti == "0100" && field == 39) return false;
    return true;
}
