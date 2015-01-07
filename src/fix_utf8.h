#pragma once
#include <string>

// Fix UTF-8 byte sequence.
// Invalid bytes are encoded in UTF-8B (using code points U+DC80..U+DCFF)
// http://permalink.gmane.org/gmane.comp.internationalization.linux/920
//
// Rejecting:
// — truncated sequences
// - lone continuation bytes
// - UTF-8 sequences longer then 4 bytes
// - code points above 0x10FFFF
// - overlong encoding
// - code points in surrogates range (for unambigious decoding of UTF-8B)
void fix_utf8(std::string &result,
              const unsigned char *i, const unsigned char *end);

// Some variations
size_t fix_utf8(void *buf,
                const unsigned char *i, const unsigned char *end);
