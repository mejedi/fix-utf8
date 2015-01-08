#include "fix_utf8.h"
#include <cstdlib>

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

// Templated Sink allows us to play with different methods for building
// the output to estimate the relative efficiency of various approaches
// (ex: a large buffer with no bounds checking vs. std::string).
//
// The handling of invalid bytes is also configurable via the sink.
template <typename Sink>
__attribute__((__always_inline__))
const unsigned char *
fix_utf8_engine(Sink &sink,
                const unsigned char *i, const unsigned char *end)
{
    while (i < end) {

        // for sinks with limited capacity
        if (!sink.check_capacity())
            return i;

        // ensure the second byte is available in a multibyte UTF-8 sequence
        ASM_COMMENT("last byte of input?");
        if (__builtin_expect(i+1 == end, 0) && i[0] > 0x7f)
            goto bad_utf8;

        switch (i[0]) {

            case 0x00 ... 0x7f:
                ASM_COMMENT("1-byte");
                // 1-byte UTF-8 sequence
                // make output
                sink.template write<1>(i);
                i += 1;
                continue;

            case 0xc2 ... 0xdf:
                ASM_COMMENT("2-byte");
                // 2-byte UTF-8 sequence
                if (!utf8_contb(i[1]))
                    goto bad_utf8;
                // make output
                sink.template write<2>(i);
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
                if (i+2 >= end || !utf8_contb(i[1]))
                    goto bad_utf8;
                if (!utf8_contb(i[2]))
                    goto bad_utf8_2;
                // make output
                sink.template write<3>(i);
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
                if (i+3 >= end || !utf8_contb(i[1]))
                    goto bad_utf8;
                if (!utf8_contb(i[2]) || !utf8_contb(i[3]))
                    goto bad_utf8_2;
                // make output
                sink.template write<4>(i);
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

    bad_utf8_2:
        ASM_COMMENT("bad-utf8-2");
        // optimization in the case we know there are 2 invalid bytes
        sink.write_bad(i);
        i += 1;

    bad_utf8:
        ASM_COMMENT("bad-utf8");
        // encoding just one byte even if invalid sequence was longer
        // (ok due to self-sync property of UTF-8)
        sink.write_bad(i);
        i += 1;
        continue;
    }

    return end;
}

// Helper functions to produce #1, #2 and #3 byte of the UTF8-B encoding
inline unsigned char utf8b_1(unsigned char c) { return 0xed; }
inline unsigned char utf8b_2(unsigned char c) { return 0xac + (c >> 6); }
inline unsigned char utf8b_3(unsigned char c) { return 0x80 | (c & 0x3f); }

// Write output to a big buffer (no bounds checking)
// probably the fastest option
struct big_buf_sink
{
    unsigned char *p_;
    big_buf_sink(void *p): p_(static_cast<unsigned char *>(p)) {}
    bool check_capacity() { return true; }
    template<size_t n> void write(const unsigned char *p);
    void write_bad(const unsigned char *p) {
        unsigned char c = p[0];
        p_[0] = utf8b_1(c);
        p_[1] = utf8b_2(c);
        p_[2] = utf8b_3(c);
        p_ += 3;
    }
};
// Initially there used to be a single write<> implementation.
// It contained a loop and the loop was successfully unrolled. However
// the generated code was still inefficient since the compiler assumed
// aliazing (p[i] was fetched from memory though it was still available
// from a register; this was done because a store to p_[i-1] could have
// invalidated p[i]).
//
// Probably it was possible to fix this issue by placing a few
// 'restrict' keywords strategically but I didn't manage to.
template<> void big_buf_sink::write<1>(const unsigned char *p) {
    p_[0] = p[0];
    p_ += 1;
}
template<> void big_buf_sink::write<2>(const unsigned char *p) {
    unsigned a = p[0], b = p[1];
    p_[0] = a; p_[1] = b;
    p_ += 2;
}
template<> void big_buf_sink::write<3>(const unsigned char *p) {
    unsigned a = p[0], b = p[1], c = p[2];
    p_[0] = a; p_[1] = b; p_[2] = c;
    p_ += 3;
}
template<> void big_buf_sink::write<4>(const unsigned char *p) {
    unsigned a = p[0], b = p[1], c = p[2], d = p[3];
    p_[0] = a; p_[1] = b; p_[2] = c; p_[3] = d;
    p_ += 4;
}

// Write output to malloc-ed buf (auto grow)
struct malloc_buf_sink: big_buf_sink
{
    unsigned char *begin_, *end_;
    malloc_buf_sink(void *p, size_t size): big_buf_sink(p), begin_(p_), end_(p_+size) {}
    bool check_capacity() {
        if (__builtin_expect(p_ + 6 >= end_, 0)) {

            size_t data_size = p_ - begin_;
            size_t size = end_ - begin_, next_size = size + size/2;
            void *buf = realloc(begin_, next_size);
            begin_ = reinterpret_cast<unsigned char *>(buf);
            end_ = begin_ + next_size;
            p_ = begin_ + data_size;
        }
        return true;
    }
};

// Write output to std::string
struct std_string_sink
{
    std::string &s_;
    std_string_sink(std::string &s): s_(s) {}
    bool check_capacity() { return true; }
    template<size_t n> void write(const unsigned char *p) {
        s_.append(p, p+n);
    }
    void write_bad(const unsigned char *p) {
        unsigned char esc[] = {
            utf8b_1(*p),
            utf8b_2(*p),
            utf8b_3(*p)
        };
        s_.append(esc, esc + 3);
    }
};

// Write output to std::vector
struct std_vector_sink
{
    std::vector<unsigned char> &v_;
    std_vector_sink(std::vector<unsigned char> &v): v_(v) {}
    bool check_capacity() { return true; }
    template<size_t n> void write(const unsigned char *p) {
        v_.insert(v_.end(), p, p+n);
    }
    void write_bad(const unsigned char *p) {
        unsigned char esc[] = {
            utf8b_1(*p),
            utf8b_2(*p),
            utf8b_3(*p)
        };
        v_.insert(v_.end(), esc, esc + 3);
    }
};

} // namespace {

size_t fix_utf8(void *buf,
                const unsigned char *i, const unsigned char *end)
{
    big_buf_sink sink(buf);
    fix_utf8_engine(sink, i, end);
    return sink.p_ - static_cast<const unsigned char *>(buf);
}

size_t fix_utf8(void **pbuf,
                const unsigned char *i, const unsigned char *end)
{
    size_t size = end - i;
    void *buf = malloc(size);
    malloc_buf_sink sink(buf, size);
    fix_utf8_engine(sink, i, end);
    *pbuf = sink.begin_;
    return sink.p_ - sink.begin_;
}

void fix_utf8(std::string &result,
              const unsigned char *i, const unsigned char *end)
{
    result.reserve(result.size() + (end - i));
    std_string_sink sink(result);
    fix_utf8_engine(sink, i, end);
}

void fix_utf8(std::vector<unsigned char> &result,
              const unsigned char *i, const unsigned char *end)
{
    result.reserve(result.size() + (end - i));
    std_vector_sink sink(result);
    fix_utf8_engine(sink, i, end);
}
