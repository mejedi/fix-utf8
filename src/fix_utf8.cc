#include "fix_utf8.h"

// Make navigating generated assembly manageable (for dummies like me).
// Ensure it doesn't change the generated code except for comments,
// compile with -DNO_ASM_COMMENTS=1 and run diff.
#if ! NO_ASM_COMMENTS
#define ASM_COMMENT(comment) __asm("# " comment)
#else
#define ASM_COMMENT(comment)
#endif

namespace {

// UTF-8 continuation byte?
inline bool utf8_contb(unsigned char c) { return (c & 0xc0) == 0x80; }

}

void fix_utf8(std::string &result,
              const unsigned char *i, const unsigned char *end)
{
    result.reserve(result.size() + end - i);

    while (i < end) {

        // ensure the second byte is available in a multibyte UTF-8 sequence
        ASM_COMMENT("last byte of input?");
        if (__builtin_expect(i+1 == end, 0) && i[0] > 0x7f)
            goto bad_utf8;

        switch (i[0]) {

            case 0x00 ... 0x7f:
                ASM_COMMENT("1-byte");
                // 1-byte UTF-8 sequence
                // make output
                result.append(i, i+1);
                i += 1;
                continue;

            case 0xc2 ... 0xdf:
                ASM_COMMENT("2-byte");
                // 2-byte UTF-8 sequence
                if (!utf8_contb(i[1]))
                    goto bad_utf8;
                // make output
                result.append(i, i+2);
                i += 2;
                continue;

            case 0xe0:
                ASM_COMMENT("3-byte overlong?");
                // 3-byte UTF-8 sequence (maybe overlong encoding)
                if (i[1] <= 0x9f)
                    goto bad_utf8;
                if (0) {
                case 0xed:
                    // 3-byte UTF-8 sequence (maybe surrogate)
                    if (i[1] > 0x9f)
                        goto bad_utf8;
                }
                // fallthrough
            case 0xe1 ... 0xec:
            case 0xee:
            case 0xef:
                ASM_COMMENT("3-byte");
                // 3-byte UTF-8 sequence
                if (i+2 >= end || !utf8_contb(i[1]) || !utf8_contb(i[2]))
                    goto bad_utf8;
                // make output
                result.append(i, i+3);
                i += 3;
                continue;

            case 0xf0:
                ASM_COMMENT("4-byte overlong?");
                // 4-byte UTF-8 sequence (maybe overlong encoding)
                if (i[1] <= 0x8f)
                    goto bad_utf8;
                if (0) {
                case 0xf4:
                    // 4-byte UTF-8 sequence (probably a code point
                    //     greater then 0x10ffff which is invalid)
                    if (i[1] > 0x8f)
                        goto bad_utf8;
                }
                // fallthrough
            case 0xf1 ... 0xf3:
                ASM_COMMENT("4-byte");
                // 4-byte UTF-8 sequence
                if (i+3 >= end || !utf8_contb(i[1]) ||
                        !utf8_contb(i[2]) || !utf8_contb(i[3]))
                    goto bad_utf8;
                // make output
                result.append(i, i+4);
                i += 4;
                continue;

            case 0x80 ... 0xbf:
                // UTF-8 continuation byte
            case 0xc0:
            case 0xc1:
                // 2-byte UTF-8 sequence (overlong encoding)
            case 0xf5 ... 0xff:
                // invalid
                goto bad_utf8;
        }

    bad_utf8:
        ASM_COMMENT("bad-utf8");
        // encoding just one byte even if invalid sequence was longer
        // (ok due to self-sync property of UTF-8)
        unsigned char enc[3];
        enc[0] = 0xed;
        enc[1] = 0xac + (i[0]>>6);
        enc[2] = 0x80 | (i[0] & 0x3f);
        result.append(enc, enc+3);
        i += 1;
        continue;
    }
}
