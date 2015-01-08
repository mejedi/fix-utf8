#include "fix_utf8.h"

#include <sys/time.h> // gettimeofday TODO more c++ way

#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <memory>
#include <functional>
#include <algorithm>
#include <array>

typedef std::mt19937 Rnd;

// a sample generator (base class)
struct SGen
{
    SGen(): prio(1.0) {}
    virtual ~SGen() {}
    virtual void gen_next(std::string &out, Rnd &rnd) = 0;
    void generate(std::string &out, size_t n, Rnd &rnd)
    {
        n += out.size();
        out.reserve(n);
        while (out.size() < n)
            gen_next(out, rnd);
    }
    double prio;
};

// configure non-default priority
std::unique_ptr<SGen> priority(double prio, std::unique_ptr<SGen> sgen)
{
    sgen->prio = prio;
    return sgen;
}

// simple byte generator
struct Bytes: public SGen
{
    Bytes(unsigned char lo, unsigned char hi): gen_(lo, hi) {}
    virtual void gen_next(std::string &out, Rnd &rnd)
    {
        out.push_back(gen_(rnd));
    }
    std::uniform_int_distribution<unsigned char> gen_;
};

std::unique_ptr<SGen> bytes(
    unsigned char lo = 0x00,
    unsigned char hi = 0xff)
{
    return std::unique_ptr<SGen>(new Bytes(lo, hi));
}

// UTF-8 sequences
struct Utf8: public SGen
{
    Utf8(long lo, long hi): gen_(lo, hi) {}
    virtual void gen_next(std::string &out, Rnd &rnd)
    {
        long code = gen_(rnd);
        if (code < 128) {
            out.push_back(code);
            return;
        }
        unsigned char buf[16], *p = buf+15;
        long m = 0x3f;
        while (code > m && m > 1) {
            *(p -- ) = 0x80 | (0x3f & code);
            code >>= 6;
            m >>= 1;
        }
        *p = ((~m)<<1) | (m & code);
        out.append(p, buf+16);
    }
    std::uniform_int_distribution<long> gen_;
};

std::unique_ptr<SGen> utf8(long lo = 0, long hi = 0x10ffff)
{
    return std::unique_ptr<SGen>(new Utf8(lo, hi));
}

// UTF8 sequences (invalid)
struct Utf8Substr: public Utf8
{
    Utf8Substr(int lo, int hi, long code_lo, long code_hi):
        Utf8(code_lo, code_hi), gen_(lo, hi) {}
    virtual void gen_next(std::string &out, Rnd &rnd)
    {
        size_t pos = out.size();
        Utf8::gen_next(out, rnd);
        size_t len = out.size() - pos;
        int idx = 0;
        while (!idx) idx = gen_(rnd);
        if (idx < 0) {
            // cut tail
            out.erase(
                out.end() - std::min(-idx, (int)len - 1),
                out.end());
        } else {
            // cut head
            out.erase(
                out.end() - len,
                out.end() - len + std::min(idx, (int)len - 1));
        }
    }
    std::uniform_int_distribution<int> gen_;
};

std::unique_ptr<SGen> utf8_substr(
    int lo = -4, int hi = 4,
    long code_lo = 0, long code_hi = 0x10ffff)
{
    return std::unique_ptr<SGen>(new Utf8Substr(lo, hi, code_lo, code_hi));
}

// Mix
struct Mix: public SGen
{
    Mix(std::vector<std::unique_ptr<SGen>> &&nodes):
        gen_(0.0, prio_sum(nodes)), nodes_(std::move(nodes)) {}
    static double prio_sum(
            const std::vector<std::unique_ptr<SGen>> &nodes) {
        double sum = 0.0;
        for (auto &node: nodes)
            sum += node->prio;
        return sum;
    }
    virtual void gen_next(std::string &out, Rnd &rnd)
    {
        double sel = gen_(rnd);
        for (auto &node: nodes_) {
            if (node->prio < sel)
                return node->gen_next(out, rnd);
            sel -= node->prio;
        }
    }
    std::uniform_real_distribution<> gen_;
    std::vector<std::unique_ptr<SGen>> nodes_;
};

// I am a dummy, what's the proper way of doing this stuff?
void mix_helper__(
    std::vector<std::unique_ptr<SGen>> &nodes) {}
template<typename... Args>
void mix_helper__(
    std::vector<std::unique_ptr<SGen>> &nodes,
    std::unique_ptr<SGen> node, Args... args)
{
    nodes.push_back(std::move(node));
    mix_helper__(nodes, std::forward<Args>(args)...);
}

template<typename... Args>
std::unique_ptr<SGen> mix(Args... args)
{
    std::vector<std::unique_ptr<SGen>> nodes;
    mix_helper__(nodes, std::forward<Args>(args)...);
    return std::unique_ptr<SGen> (new Mix(std::move(nodes)));
}

std::string make_sample(
    size_t sample_size,
    std::unique_ptr<SGen> node)
{
    Rnd rnd;
    std::string res;
    node->generate(res, sample_size, rnd);
    return res;
}

int main()
{
    class Ts
    {
        public:
            Ts() { gettimeofday(&tv, 0); };
            double operator - (const Ts &other)
            {
                timeval delta;
                timersub(&tv, &other.tv, &delta);
                return (double)delta.tv_sec + (double)delta.tv_usec *
                    1e-6;
            }
        private:
            timeval tv;
    };

    std::cerr << "Generating samples..." << std::endl;

    const size_t sample_size = 8 * 1024 * 1024;

    // Generator vernacular, revisited:
    //
    // bytes(opt lo, opt hi) -    bytes in [lo, hi]
    // utf8(opt lo, opt hi)  -    UTF-8 encoding of codes in [lo, hi]
    // utf8_substr(...) -         truncated UTF-8 encoding (invalid)
    // mix(...) -                 combine several generators
    // priority(val, gen) -       use in mix
    const std::vector<std::pair<std::string, std::string>> samples = {

        {"Random", make_sample(sample_size,
            bytes())},

        {"ASCII", make_sample(sample_size,
            bytes(0, 127))},

        {"\"Unicode(small)\"", make_sample(sample_size,
            utf8(0, 0x7ff))},

        {"\"Unicode(full)\"", make_sample(sample_size,
            utf8())},

        {"\"Unicode(evil mix)\"", make_sample(sample_size,
            mix(
                priority(5.0, utf8()),
                bytes(0x80, 0xc2),
                bytes(0xf5, 0xff),
                utf8(0xd800, 0xdfff),
                utf8(0x110000, 0x1fffff),
                utf8_substr()))},

        {"\"Unicode(evil short)\"", make_sample(sample_size,
            mix(
                bytes(0x80, 0xc2),
                bytes(0xf5, 0xff)))},

        {"\"Unicode(evil long)\"", make_sample(sample_size,
            utf8_substr(-1,0,0x10000,0x10ffff))},
    };

    // Contestants
    const std::vector<std::pair<
        std::string,
        std::function<double(const unsigned char *, const unsigned char *)>>>
            contestants = {

                {"baseline", [](
                    const unsigned char *i, const unsigned char *end) {
                        std::vector<unsigned char>buf;
                        buf.resize((end - i)*3);

                        Ts start_ts;
                        fix_utf8(&buf[0], i, end);
                        Ts stop_ts;

                        return stop_ts - start_ts;
                }},

                {"string  ", [](
                    const unsigned char *i, const unsigned char *end) {
                        Ts start_ts;
                        std::string res;
                        fix_utf8(res, i, end);
                        Ts stop_ts;
                        return stop_ts - start_ts;
                }}

            };

    std::cout << "#";
    for (auto &sample: samples) {
        std::cout << " " << sample.first;
    }
    std::cout << std::endl;

    for (auto &contestant: contestants) {

        std::cout << contestant.first;

        for (auto &sample: samples) {

            const unsigned char *i =
                reinterpret_cast<const unsigned char *>(
                    sample.second.c_str());
            const unsigned char *end = i + sample.second.size();

            const int N = 7;
            std::array<double, N> res;
            for (int j=0; j<N; j++)
                res[j] = contestant.second(i, end);

            std::sort(res.begin(), res.end());

            double avg =
                std::accumulate(res.begin()+1, res.end()-1, 0.0) / (res.size()-2);

            std::cout << "\t " << std::fixed << std::setprecision(5) << avg;
        }

        std::cout << std::endl;

    }

    return 0;
}
