#include "fix_utf8.h"
#include <gtest/gtest.h>
#include <string>
#include <initializer_list>

std::string
to_json_string(const std::string &s);

namespace {

std::string utf8_encode(unsigned long code_point, int width = 0);
std::string utf8b_encode(const std::string &input);

struct SBit {
    SBit(const std::string &input_, const std::string &expected_output_)
        : input(input_), expected_output(expected_output_) {}
    SBit(unsigned long code_point)
        : input(utf8_encode(code_point)), expected_output(input) {}
    SBit(const char *s)
        : input(s), expected_output(input) {}
    SBit(std::initializer_list<SBit> bits) {
        for (auto& bit: bits) {
            input += bit.input;
            expected_output += bit.expected_output;
        }
    }
    std::string input, expected_output;
};

std::pair<std::string,std::string>
fix_utf8_test(std::initializer_list<SBit> bits)
{
    SBit setup(bits);
    return std::make_pair(setup.expected_output,
        to_json_string(setup.input));
}
// UGLY, but reports the correct __LINE__
#define fix_utf8_test(...) \
    do { auto res__ = fix_utf8_test(__VA_ARGS__); \
        ASSERT_EQ(res__.first, res__.second); } while (0)

// helpers for fix_utf8_test
SBit bad_str(const std::string &s) { return SBit(s, utf8b_encode(s)); }
SBit bad_code(unsigned long code_point, int width = 0)
{
    return bad_str(utf8_encode(code_point, width));
}

std::string utf8_encode(unsigned long code, int width)
{
    if (width == 0) {
        // autodetect
        if (code < 0x80)
            width = 1;
        else if (code < 0x800)
            width = 2;
        else if (code < 0x10000)
            width = 3;
        else if (code < 0x200000)
            width = 4;
        else if (code < 0x4000000)
            width = 5;
        else
            width = 6;
    }
    if (width == 1)
        return std::string(1, 0x7f & code);

    std::string res;
    unsigned char m = -1 << (8 - width);
    res.push_back(m | ((m - 1) & (code >> (6 * (width - 1)))));
    for (int i = width - 2; i >= 0; i--)
        res.push_back(0x80 | (0x3f & (code >> (6 * i))));
    return res;
}

std::string utf8b_encode(const std::string &input)
{
    std::string res;
    for (unsigned c: input)
        res.append(utf8_encode(0xDC00 + c));
    return res;
}

} // namespace {

///////////////////////////////////////////////////////////////////////

TEST(sanity_check, utf8_encode) {
    ASSERT_EQ("$", utf8_encode('$'));
    ASSERT_EQ("\x7f", utf8_encode(0x7f));
    ASSERT_EQ("\xc2\x80", utf8_encode(0x80));
    ASSERT_EQ("\xc2\xa2", utf8_encode(0xa2));
    ASSERT_EQ("\xdf\xbf", utf8_encode(0x7ff));
    ASSERT_EQ("\xe0\xa0\x80", utf8_encode(0x800));
    ASSERT_EQ("\xe2\x82\xac", utf8_encode(0x20ac));
    ASSERT_EQ("\xef\xbf\xbf", utf8_encode(0xffff));
    ASSERT_EQ("\xf0\x90\x8d\x88", utf8_encode(0x10348));
}
TEST(sanity_check, utf8_encode_overlong) {
    ASSERT_EQ("\xc0\x80", utf8_encode(0, 2));
    ASSERT_EQ("\xf0\x82\x82\xac", utf8_encode(0x20ac, 4));
}
TEST(sanity_check, utf8b_encode) {
    ASSERT_EQ(
        "\xED\xAF\xB0"
        "\xED\xAE\x82"
        "\xED\xAE\x82"
        "\xED\xAE\xAC",
        utf8b_encode("\xf0\x82\x82\xac"));
}

///////////////////////////////////////////////////////////////////////

TEST(utf8_fix, good) {
    fix_utf8_test({});
    fix_utf8_test({"Hello, world!"});
    fix_utf8_test({"Hello, ", 0x80, "/",  0x800, 0x1000, "!"});
    fix_utf8_test({
        0x080, 0x0c0, 0x100, 0x140, 0x180, 0x1c0, 0x200, 0x240,
        0x2b0, 0x2c0, 0x300, 0x340, 0x3b0, 0x3c0});
    fix_utf8_test({
        0x400, 0x440, 0x480, 0x4c0, 0x500, 0x540, 0x5b0, 0x5c0,
        0x600, 0x640, 0x6b0, 0x6c0, 0x700, 0x740, 0x7b0, 0x7c0});
    fix_utf8_test({
        0x0800, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000,
        0x8000, 0x9000, 0xa000, 0xb000, 0xc000, 0xd000, 0xe000, 0xf000});
    fix_utf8_test({
        0x10000, 0x40000, 0xb0000, 0xc0000, 0x10000});
}
TEST(utf8_fix, bad_bytes) {
    fix_utf8_test({bad_str("\x80\x81\x82\x83\x84\x85\x86\x87")});
    fix_utf8_test({bad_str("\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f")});
    fix_utf8_test({bad_str("\x90\x91\x92\x93\x94\x95\x96\x97")});
    fix_utf8_test({bad_str("\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f")});
    fix_utf8_test({bad_str("\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7")});
    fix_utf8_test({bad_str("\xa8\xa9\xaa\xab\xac\xad\xae\xaf")});
    fix_utf8_test({bad_str("\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7")});
    fix_utf8_test({bad_str("\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf")});
    fix_utf8_test({bad_str("\xc0\xc1")});
    fix_utf8_test({bad_str("\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff")});
}
TEST(utf8_fix, truncated_seq) {
    fix_utf8_test({bad_str("\xc2")});
    fix_utf8_test({bad_str("\xc2"), "test"});
    fix_utf8_test({bad_str("\xe0\xa0")});
    fix_utf8_test({bad_str("\xe0\xa0"), "test"});
    fix_utf8_test({bad_str("\xf0\x90\x8d")});
    fix_utf8_test({bad_str("\xf0\x90\x8d"), "test"});
}
TEST(utf8_fix, _5bplus) {
    fix_utf8_test({
        bad_code(0x0200000),
        bad_code(0x4000000)});
}
TEST(utf8_fix, max_code_point) {
    fix_utf8_test({0x10ffff});
    fix_utf8_test({bad_code(0x110000)});
    fix_utf8_test({bad_code(0x1fffff)});
}
TEST(utf8_fix, overlong_enc) {
    fix_utf8_test({bad_code(0, 2)});
    fix_utf8_test({bad_code(0x7f, 2)});
    fix_utf8_test({0x80});
    fix_utf8_test({0x7ff});
    fix_utf8_test({bad_code(0x7ff, 3)});
    fix_utf8_test({0x800});
    fix_utf8_test({0xffff});
    fix_utf8_test({bad_code(0xffff, 4)});
    fix_utf8_test({0x10000});
}
TEST(utf8_fix, surrogates) {
    fix_utf8_test({0xd800 - 1});
    fix_utf8_test({bad_code(0xd800)});
    fix_utf8_test({bad_code(0xdfff)});
    fix_utf8_test({0xdfff + 1});
}
