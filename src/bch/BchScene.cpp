#include "bch/BchScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace promptemon::bch {
namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint16_t u16(const Bytes& b, std::size_t o) {
    if (o + 2 > b.size()) throw std::runtime_error("BCH truncated");
    return static_cast<std::uint16_t>(b[o]) |
           (static_cast<std::uint16_t>(b[o + 1]) << 8);
}

std::int16_t s16(const Bytes& b, std::size_t o) {
    return static_cast<std::int16_t>(u16(b, o));
}

std::uint32_t u32(const Bytes& b, std::size_t o) {
    if (o + 4 > b.size()) throw std::runtime_error("BCH truncated");
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint64_t u64(const Bytes& b, std::size_t o) {
    if (o + 8 > b.size()) throw std::runtime_error("BCH truncated");
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(b[o + i]) << (i * 8);
    return value;
}

void put32(Bytes& b, std::size_t o, std::uint32_t v) {
    if (o + 4 > b.size()) throw std::runtime_error("BCH relocation out of bounds");
    for (int i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (i * 8));
}

float f32(const Bytes& b, std::size_t o) {
    const auto v = u32(b, o);
    float f = 0;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}

std::string cstr(const Bytes& b, std::size_t o) {
    if (!o || o >= b.size()) return {};
    std::size_t e = o;
    while (e < b.size() && b[e]) ++e;
    return std::string(reinterpret_cast<const char*>(b.data() + o), e - o);
}

struct Header {
    std::uint8_t bc = 0;
    std::uint32_t main = 0;
    std::uint32_t str = 0;
    std::uint32_t gpu = 0;
    std::uint32_t data = 0;
    std::uint32_t dataExt = 0;
    std::uint32_t reloc = 0;
    std::uint32_t relocLen = 0;
};

Header relocate(Bytes& b) {
    if (b.size() < 0x40 || b[0] != 'B' || b[1] != 'C' || b[2] != 'H') {
        throw std::runtime_error("Not BCH");
    }

    Header h;
    h.bc = b[4];
    h.main = u32(b, 8);
    h.str = u32(b, 12);
    h.gpu = u32(b, 16);
    h.data = u32(b, 20);
    if (h.bc > 0x20) {
        h.dataExt = u32(b, 24);
        h.reloc = u32(b, 28);
        h.relocLen = u32(b, 52);
    } else {
        h.reloc = u32(b, 24);
        h.relocLen = u32(b, 44);
    }

    const std::uint64_t relocationEnd =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(h.reloc) + h.relocLen, b.size());

    for (std::uint32_t o = h.reloc; static_cast<std::uint64_t>(o) + 4 <= relocationEnd; o += 4) {
        const auto value = u32(b, o);
        const auto offset = value & 0x1ffffffu;
        const auto flags = value >> 25;

        const auto patch = [&](std::uint64_t p, std::uint32_t base, std::uint32_t high = 0) {
            if (p + 4 > b.size()) return;
            auto x = u32(b, static_cast<std::size_t>(p)) + base;
            x = (x & 0x7fffffffu) | high;
            put32(b, static_cast<std::size_t>(p), x);
        };

        if (flags == 0) patch(static_cast<std::uint64_t>(offset) * 4 + h.main, h.main);
        else if (flags == 1) patch(static_cast<std::uint64_t>(offset) + h.main, h.str);
        else if (flags == 2) patch(static_cast<std::uint64_t>(offset) * 4 + h.main, h.gpu);
        else if (flags == 7 || flags == 0x0c) patch(static_cast<std::uint64_t>(offset) * 4 + h.main, h.data);

        const std::uint64_t gpuPointer = static_cast<std::uint64_t>(offset) * 4 + h.gpu;
        if (h.bc >= 0x21) {
            if (flags == 0x25 || flags == 0x26 || flags == 0x2e) patch(gpuPointer, h.data);
            else if (flags == 0x27) patch(gpuPointer, h.data, 0x80000000u);
            else if (flags == 0x28) patch(gpuPointer, h.data);
            else if (flags == 0x2b) patch(gpuPointer, h.dataExt);
            else if (flags == 0x2c) patch(gpuPointer, h.dataExt, 0x80000000u);
            else if (flags == 0x2d) patch(gpuPointer, h.dataExt);
        }
    }
    return h;
}

Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
        }
    }
    return r;
}

Mat4 translation(Vec3 v) {
    auto r = identity();
    r.m[12] = v.x;
    r.m[13] = v.y;
    r.m[14] = v.z;
    return r;
}

Mat4 scale(Vec3 v) {
    auto r = identity();
    r.m[0] = v.x;
    r.m[5] = v.y;
    r.m[10] = v.z;
    return r;
}

Quat normalize(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-8f) return {0, 0, 0, 1};
    q.x /= len;
    q.y /= len;
    q.z /= len;
    q.w /= len;
    return q;
}

Mat4 qmat(Quat q) {
    q = normalize(q);
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    auto r = identity();
    r.m[0] = 1 - 2 * y * y - 2 * z * z;
    r.m[4] = 2 * x * y - 2 * z * w;
    r.m[8] = 2 * x * z + 2 * y * w;
    r.m[1] = 2 * x * y + 2 * z * w;
    r.m[5] = 1 - 2 * x * x - 2 * z * z;
    r.m[9] = 2 * y * z - 2 * x * w;
    r.m[2] = 2 * x * z - 2 * y * w;
    r.m[6] = 2 * y * z + 2 * x * w;
    r.m[10] = 1 - 2 * x * x - 2 * y * y;
    return r;
}

Quat euler(Vec3 r) {
    const float cx = std::cos(r.x * 0.5f), sx = std::sin(r.x * 0.5f);
    const float cy = std::cos(r.y * 0.5f), sy = std::sin(r.y * 0.5f);
    const float cz = std::cos(r.z * 0.5f), sz = std::sin(r.z * 0.5f);
    return normalize({
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    });
}

Mat4 trs(Vec3 t, Quat r, Vec3 s) {
    return mul(translation(t), mul(qmat(r), scale(s)));
}

Vec3 transformPoint(const Mat4& m, Vec3 v) {
    return {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]
    };
}

Mat4 inverseAffine(const Mat4& m) {
    const float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    const float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    const float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    const float det = a00 * (a11 * a22 - a12 * a21) -
                      a01 * (a10 * a22 - a12 * a20) +
                      a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(det) < 1e-10f) return identity();

    const float invDet = 1.0f / det;
    auto r = identity();
    r.m[0] = (a11 * a22 - a12 * a21) * invDet;
    r.m[4] = (a02 * a21 - a01 * a22) * invDet;
    r.m[8] = (a01 * a12 - a02 * a11) * invDet;
    r.m[1] = (a12 * a20 - a10 * a22) * invDet;
    r.m[5] = (a00 * a22 - a02 * a20) * invDet;
    r.m[9] = (a02 * a10 - a00 * a12) * invDet;
    r.m[2] = (a10 * a21 - a11 * a20) * invDet;
    r.m[6] = (a01 * a20 - a00 * a21) * invDet;
    r.m[10] = (a00 * a11 - a01 * a10) * invDet;

    const Vec3 t{m.m[12], m.m[13], m.m[14]};
    const Vec3 nt = transformPoint(r, {-t.x, -t.y, -t.z});
    r.m[12] = nt.x;
    r.m[13] = nt.y;
    r.m[14] = nt.z;
    return r;
}

struct Pica {
    std::vector<std::uint32_t> cmd = std::vector<std::uint32_t>(0x10000);
    std::array<std::vector<float>, 96> uniform{};

    Pica(const Bytes& b, std::size_t p, std::uint32_t words) {
        std::uint32_t readWords = 0;
        std::uint32_t currentUniform = 0;

        while (readWords < words && p + 8 <= b.size()) {
            const auto parameter = u32(b, p);
            const auto header = u32(b, p + 4);
            p += 8;
            readWords += 2;

            std::uint16_t id = static_cast<std::uint16_t>(header);
            const auto mask = (header >> 16) & 0xf;
            const auto extra = (header >> 20) & 0x7ff;
            const bool consecutive = (header & 0x80000000u) != 0;
            std::vector<float> values;

            const auto write = [&](std::uint16_t command, std::uint32_t value) {
                cmd[command] = (cmd[command] & ((~mask) & 0xf)) |
                               (value & (0xfffffff0u | mask));
            };

            write(id, parameter);
            if (id == 0x23d) break;
            if (id == 0x2c0) {
                currentUniform = parameter & 0x7fffffff;
            } else if (id == 0x2c1) {
                float f = 0;
                const auto raw = cmd[id];
                std::memcpy(&f, &raw, sizeof(f));
                values.push_back(f);
            }

            for (std::uint32_t i = 0; i < extra && p + 4 <= b.size(); ++i) {
                if (consecutive) ++id;
                write(id, u32(b, p));
                p += 4;
                ++readWords;
                if (id > 0x2c0 && id < 0x2c9) {
                    float f = 0;
                    const auto raw = cmd[id];
                    std::memcpy(&f, &raw, sizeof(f));
                    values.push_back(f);
                }
            }

            if (currentUniform < uniform.size() && !values.empty()) {
                uniform[currentUniform].insert(uniform[currentUniform].end(), values.begin(), values.end());
            }
            p = (p + 7) & ~std::size_t(7);
        }
    }

    std::uint32_t param(int id) const { return cmd[static_cast<std::size_t>(id)]; }

    std::array<int, 23> mainPermutation() const {
        const std::uint64_t value = param(0x2bb) | (static_cast<std::uint64_t>(param(0x2bc)) << 32);
        std::array<int, 23> out{};
        for (int i = 0; i < 23; ++i) out[i] = static_cast<int>((value >> (i * 4)) & 0xf);
        return out;
    }

    std::array<int, 23> bufferPermutation() const {
        const std::uint64_t value = param(0x204) |
            (static_cast<std::uint64_t>(param(0x205) & 0xffff) << 32);
        std::array<int, 23> out{};
        for (int i = 0; i < 23; ++i) out[i] = static_cast<int>((value >> (i * 4)) & 0xf);
        return out;
    }

    std::array<std::pair<int, int>, 23> formats() const {
        const std::uint64_t value = param(0x201) |
            (static_cast<std::uint64_t>(param(0x202)) << 32);
        std::array<std::pair<int, int>, 23> out{};
        for (int i = 0; i < 23; ++i) {
            const int v = static_cast<int>((value >> (i * 4)) & 0xf);
            out[i] = {v & 3, v >> 2};
        }
        return out;
    }

    int stride() const { return static_cast<int>((param(0x205) >> 16) & 0xff); }
    int totalAttributes() const { return static_cast<int>((param(0x205) >> 28) & 0xf); }
    std::uint32_t vertexAddress() const { return param(0x203); }
    std::uint32_t indexAddress() const { return param(0x227) & 0x7fffffffu; }
    bool index16() const { return (param(0x227) >> 31) != 0; }
    std::uint32_t indexCount() const { return param(0x228); }
};

std::vector<float> readAttribute(const Bytes& b, std::size_t& p, int type, int count) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (type == 0) {
            out.push_back(static_cast<std::int8_t>(b.at(p++)));
        } else if (type == 1) {
            out.push_back(b.at(p++));
        } else if (type == 2) {
            out.push_back(s16(b, p));
            p += 2;
        } else {
            out.push_back(f32(b, p));
            p += 4;
        }
    }
    return out;
}

constexpr std::array<int, 64> kTileOrder = {
    0,1,8,9,2,3,10,11,16,17,24,25,18,19,26,27,
    4,5,12,13,6,7,14,15,20,21,28,29,22,23,30,31,
    32,33,40,41,34,35,42,43,48,49,56,57,50,51,58,59,
    36,37,44,45,38,39,46,47,52,53,60,61,54,55,62,63
};

int bytesPerPixel(int format) {
    switch (format) {
        case 0: return 4;
        case 1: return 3;
        case 2: case 3: case 4: case 5: case 6: return 2;
        case 7: case 8: case 9: return 1;
        default: return 0;
    }
}

std::uint8_t expand4(std::uint32_t v) { return static_cast<std::uint8_t>((v & 0xf) * 17); }
std::uint8_t expand5(std::uint32_t v) { return static_cast<std::uint8_t>((v * 255 + 15) / 31); }
std::uint8_t expand6(std::uint32_t v) { return static_cast<std::uint8_t>((v * 255 + 31) / 63); }
std::uint8_t clampByte(int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }

void writeRgba(std::vector<std::uint8_t>& out, std::size_t pixel,
               std::uint8_t r, std::uint8_t g, std::uint8_t blue, std::uint8_t a) {
    const std::size_t o = pixel * 4;
    if (o + 4 > out.size()) return;
    out[o] = r;
    out[o + 1] = g;
    out[o + 2] = blue;
    out[o + 3] = a;
}

std::vector<std::uint8_t> decodePlainTexture(const Bytes& b, std::size_t dataOffset,
                                             int format, int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4, 255);
    const int tilesX = (width + 7) / 8;
    const int tilesY = (height + 7) / 8;

    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            for (int k = 0; k < 64; ++k) {
                const int tilePosition = kTileOrder[static_cast<std::size_t>(k)];
                const int px = tx * 8 + tilePosition % 8;
                const int py = ty * 8 + tilePosition / 8;
                if (px >= width || py >= height) continue;

                std::uint32_t value = 0;
                std::size_t byteOffset = dataOffset;
                if (format == 10 || format == 11) {
                    byteOffset += static_cast<std::size_t>((ty * tilesX + tx) * 32 + k / 2);
                    if (byteOffset >= b.size()) continue;
                    value = (b[byteOffset] >> ((k & 1) ? 4 : 0)) & 0xf;
                } else {
                    const int bpp = bytesPerPixel(format);
                    if (bpp <= 0) continue;
                    byteOffset += static_cast<std::size_t>((ty * tilesX + tx) * 64 * bpp + k * bpp);
                    if (byteOffset + static_cast<std::size_t>(bpp) > b.size()) continue;
                    for (int q = 0; q < bpp; ++q) value |= static_cast<std::uint32_t>(b[byteOffset + q]) << (q * 8);
                }

                std::uint8_t r = 255, g = 255, blue = 255, a = 255;
                switch (format) {
                    case 0:
                        a = static_cast<std::uint8_t>(value & 0xff);
                        blue = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        g = static_cast<std::uint8_t>((value >> 16) & 0xff);
                        r = static_cast<std::uint8_t>((value >> 24) & 0xff);
                        break;
                    case 1:
                        blue = static_cast<std::uint8_t>(value & 0xff);
                        g = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        r = static_cast<std::uint8_t>((value >> 16) & 0xff);
                        break;
                    case 2:
                        a = (value & 1) ? 255 : 0;
                        r = expand5((value >> 11) & 0x1f);
                        g = expand5((value >> 6) & 0x1f);
                        blue = expand5((value >> 1) & 0x1f);
                        break;
                    case 3:
                        r = expand5((value >> 11) & 0x1f);
                        g = expand6((value >> 5) & 0x3f);
                        blue = expand5(value & 0x1f);
                        break;
                    case 4:
                        a = expand4(value);
                        blue = expand4(value >> 4);
                        g = expand4(value >> 8);
                        r = expand4(value >> 12);
                        break;
                    case 5: {
                        a = static_cast<std::uint8_t>(value & 0xff);
                        const auto l = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        r = g = blue = l;
                        break;
                    }
                    case 6:
                        r = static_cast<std::uint8_t>(value & 0xff);
                        g = static_cast<std::uint8_t>((value >> 8) & 0xff);
                        blue = 0;
                        break;
                    case 7: r = g = blue = static_cast<std::uint8_t>(value); break;
                    case 8: r = g = blue = 255; a = static_cast<std::uint8_t>(value); break;
                    case 9:
                        a = expand4(value);
                        r = g = blue = expand4(value >> 4);
                        break;
                    case 10: r = g = blue = expand4(value); break;
                    case 11: r = g = blue = 255; a = expand4(value); break;
                    default: break;
                }
                writeRgba(out, static_cast<std::size_t>(py * width + px), r, g, blue, a);
            }
        }
    }
    return out;
}

constexpr int kEtcTable[8][4] = {
    {2, 8, -2, -8}, {5, 17, -5, -17}, {9, 29, -9, -29}, {13, 42, -13, -42},
    {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}
};

int signExtend3(int v) { return v >= 4 ? v - 8 : v; }

std::vector<std::uint8_t> decodeEtcTexture(const Bytes& b, std::size_t dataOffset,
                                           int width, int height, bool hasAlpha) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4, 255);
    const int blockSize = hasAlpha ? 16 : 8;
    const int tilesX = (width + 7) / 8;

    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            const std::size_t relative = static_cast<std::size_t>(
                (((x >> 3) + (y >> 3) * tilesX) * 4 + ((x >> 2) & 1) + ((y >> 2) & 1) * 2) * blockSize);
            const std::size_t off = dataOffset + relative;
            if (off + static_cast<std::size_t>(blockSize) > b.size()) continue;

            const std::uint64_t alpha64 = hasAlpha ? u64(b, off) : std::numeric_limits<std::uint64_t>::max();
            const std::uint64_t rgb64 = u64(b, hasAlpha ? off + 8 : off);
            const int flip = static_cast<int>((rgb64 >> 32) & 1);
            const int differential = static_cast<int>((rgb64 >> 33) & 1);

            int r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
            if (differential) {
                const int r1b = static_cast<int>((rgb64 >> 59) & 31);
                const int g1b = static_cast<int>((rgb64 >> 51) & 31);
                const int b1b = static_cast<int>((rgb64 >> 43) & 31);
                r1 = expand5(r1b); g1 = expand5(g1b); b1 = expand5(b1b);
                r2 = expand5(std::clamp(r1b + signExtend3(static_cast<int>((rgb64 >> 56) & 7)), 0, 31));
                g2 = expand5(std::clamp(g1b + signExtend3(static_cast<int>((rgb64 >> 48) & 7)), 0, 31));
                b2 = expand5(std::clamp(b1b + signExtend3(static_cast<int>((rgb64 >> 40) & 7)), 0, 31));
            } else {
                r1 = expand4(static_cast<std::uint32_t>((rgb64 >> 60) & 15));
                g1 = expand4(static_cast<std::uint32_t>((rgb64 >> 52) & 15));
                b1 = expand4(static_cast<std::uint32_t>((rgb64 >> 44) & 15));
                r2 = expand4(static_cast<std::uint32_t>((rgb64 >> 56) & 15));
                g2 = expand4(static_cast<std::uint32_t>((rgb64 >> 48) & 15));
                b2 = expand4(static_cast<std::uint32_t>((rgb64 >> 40) & 15));
            }

            const int cmpOne = static_cast<int>((rgb64 >> 37) & 7);
            const int cmpTwo = static_cast<int>((rgb64 >> 34) & 7);
            const std::uint32_t raw = static_cast<std::uint32_t>(rgb64);
            const std::uint32_t modulation =
                ((raw & 0xff000000u) >> 24) |
                ((raw & 0x00ff0000u) >> 8) |
                ((raw & 0x0000ff00u) << 8) |
                ((raw & 0x000000ffu) << 24);

            const auto put = [&](int px, int py, int rr, int gg, int bb, int bx, int by, int cmp) {
                if (px < 0 || py < 0 || px >= width || py >= height) return;
                const int shift = bx * 4 + by;
                int tableIndex = 0;
                if (shift < 8) {
                    tableIndex = static_cast<int>((modulation >> (shift + 24)) & 1) +
                                 static_cast<int>(((modulation << 1) >> (shift + 8)) & 2);
                } else {
                    tableIndex = static_cast<int>((modulation >> (shift + 8)) & 1) +
                                 static_cast<int>(((modulation << 1) >> (shift - 8)) & 2);
                }
                const int modifier = kEtcTable[cmp][tableIndex];
                const std::uint8_t alpha = hasAlpha
                    ? static_cast<std::uint8_t>(((alpha64 >> (shift * 4)) & 15) * 17)
                    : 255;
                writeRgba(out, static_cast<std::size_t>(py * width + px),
                          clampByte(rr + modifier), clampByte(gg + modifier), clampByte(bb + modifier), alpha);
            };

            if (flip) {
                for (int by = 0; by < 2; ++by) for (int bx = 0; bx < 4; ++bx) {
                    put(x + bx, y + by, r1, g1, b1, bx, by, cmpOne);
                    put(x + bx, y + by + 2, r2, g2, b2, bx, by + 2, cmpTwo);
                }
            } else {
                for (int by = 0; by < 4; ++by) for (int bx = 0; bx < 2; ++bx) {
                    put(x + bx, y + by, r1, g1, b1, bx, by, cmpOne);
                    put(x + bx + 2, y + by, r2, g2, b2, bx + 2, by, cmpTwo);
                }
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> decodeTexture(const Bytes& b, std::size_t dataOffset,
                                        int format, int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return {};
    if (format >= 0 && format <= 11) return decodePlainTexture(b, dataOffset, format, width, height);
    if (format == 12) return decodeEtcTexture(b, dataOffset, width, height, false);
    if (format == 13) return decodeEtcTexture(b, dataOffset, width, height, true);
    return {};
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Quat slerp(Quat a, Quat b, float t) {
    a = normalize(a);
    b = normalize(b);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize({
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        });
    }
    const float theta = std::acos(std::clamp(d, -1.0f, 1.0f));
    const float sine = std::sin(theta);
    const float aa = std::sin((1 - t) * theta) / sine;
    const float bb = std::sin(t * theta) / sine;
    return {a.x * aa + b.x * bb, a.y * aa + b.y * bb, a.z * aa + b.z * bb, a.w * aa + b.w * bb};
}

template<class T, class F>
T sample(const std::vector<T>& values, float start, float end, float frame, const T& fallback, F interpolate) {
    if (values.empty()) return fallback;
    if (values.size() == 1) return values[0];
    const float f = std::clamp(frame, start, end) - start;
    const float span = std::max(1.0f, end - start);
    const float position = f / span * static_cast<float>(values.size() - 1);
    const auto index = std::min<std::size_t>(values.size() - 2, static_cast<std::size_t>(std::floor(position)));
    return interpolate(values[index], values[index + 1], position - static_cast<float>(index));
}

} // namespace

const Animation* Scene::findAnimation(const std::string& name) const {
    for (const auto& animation : animations_) if (animation.name == name) return &animation;
    return nullptr;
}

const Texture* Scene::findTexture(const std::string& name) const {
    for (const auto& texture : textures_) if (texture.name == name) return &texture;
    return nullptr;
}

const Texture* Scene::textureForMaterial(std::uint16_t materialId) const {
    if (materialId >= materials_.size()) return nullptr;
    const auto& name = materials_[materialId].textureName;
    if (name.empty()) return nullptr;
    return findTexture(name);
}

Scene Scene::parse(const std::vector<std::uint8_t>& modelRaw,
                   const std::vector<std::uint8_t>& animRaw) {
    Scene out;
    Bytes b = modelRaw;
    const auto h = relocate(b);

    const auto modelTable = u32(b, h.main);
    const auto modelCount = u32(b, h.main + 4);
    if (!modelCount || !modelTable) throw std::runtime_error("BCH has no model");

    const auto modelHeader = u32(b, modelTable);
    if (!modelHeader || modelHeader >= b.size()) throw std::runtime_error("Invalid BCH model header");

    std::size_t p = modelHeader + 4 + 48;
    const auto materialsOffset = u32(b, p);
    const auto materialCount = u32(b, p + 4);
    p += 12;
    const auto objectsOffset = u32(b, p);
    const auto objectCount = u32(b, p + 4);
    p += 8;
    p += (h.bc > 6 ? 0x28 : 0x20);
    const auto skeletonOffset = u32(b, p);
    const auto skeletonCount = u32(b, p + 4);
    p += 12;
    const auto visibilityOffset = u32(b, p);
    (void)visibilityOffset;
    p += 8;
    out.modelName_ = cstr(b, u32(b, p));
    p += 4;
    const auto objectNameCount = u32(b, p);
    const auto objectNameOffset = u32(b, p + 4);

    const std::size_t materialStride = h.bc < 0x21 ? 0x58 : 0x2c;
    const std::size_t materialName0Offset = h.bc < 0x21 ? 0x48 : 0x1c;
    const std::size_t materialNameOffset = h.bc < 0x21 ? 0x54 : 0x28;
    for (std::uint32_t i = 0; i < materialCount; ++i) {
        const std::size_t mo = static_cast<std::size_t>(materialsOffset) + static_cast<std::size_t>(i) * materialStride;
        if (mo + materialStride > b.size()) break;
        Material material;
        material.textureName = cstr(b, u32(b, mo + materialName0Offset));
        material.name = cstr(b, u32(b, mo + materialNameOffset));

        const auto materialParametersOffset = u32(b, mo);
        if (materialParametersOffset && static_cast<std::size_t>(materialParametersOffset) + 36 <= b.size()) {
            const std::size_t coordinator0 = static_cast<std::size_t>(materialParametersOffset) + 12;
            float scaleU = f32(b, coordinator0 + 4);
            float scaleV = f32(b, coordinator0 + 8);
            const float rotation = f32(b, coordinator0 + 12);
            const float translateU = f32(b, coordinator0 + 16);
            const float translateV = f32(b, coordinator0 + 20);
            if (scaleU == 0.0f && scaleV == 0.0f) {
                scaleU = 1.0f;
                scaleV = 1.0f;
            }
            if (std::isfinite(scaleU) && std::isfinite(scaleV) && std::isfinite(rotation) &&
                std::isfinite(translateU) && std::isfinite(translateV)) {
                material.uvScaleU = scaleU;
                material.uvScaleV = scaleV;
                material.uvRotation = rotation;
                material.uvTranslateU = translateU;
                material.uvTranslateV = translateV;
            }
        }
        out.materials_.push_back(std::move(material));
    }

    if (h.main + 44 <= b.size()) {
        const auto textureTable = u32(b, h.main + 36);
        const auto textureCount = std::min<std::uint32_t>(u32(b, h.main + 40), 4096);
        for (std::uint32_t i = 0; i < textureCount; ++i) {
            const std::size_t tableEntry = static_cast<std::size_t>(textureTable) + i * 4;
            if (tableEntry + 4 > b.size()) break;
            const auto entry = u32(b, tableEntry);
            if (!entry || static_cast<std::size_t>(entry) + 32 > b.size()) continue;
            const auto commandsOffset = u32(b, entry);
            const auto commandsWords = std::min<std::uint32_t>(u32(b, entry + 4), 0x10000);
            if (!commandsOffset || static_cast<std::size_t>(commandsOffset) >= b.size()) continue;

            Pica textureCommands(b, commandsOffset, commandsWords);
            const auto sizeRegister = textureCommands.param(0x82);
            const int width = static_cast<int>((sizeRegister >> 16) & 0xffff);
            const int height = static_cast<int>(sizeRegister & 0xffff);
            const int format = static_cast<int>(textureCommands.param(0x8e) & 0xf);
            const auto dataOffset = textureCommands.param(0x85) & 0x7fffffffu;
            const auto name = cstr(b, u32(b, entry + 28));
            auto rgba = decodeTexture(b, dataOffset, format, width, height);
            if (name.empty() || rgba.empty()) continue;
            out.textures_.push_back(Texture{name, width, height, std::move(rgba)});
        }
    }

    std::vector<std::string> objectNames;
    if (objectNameOffset && objectNameOffset + 12 <= b.size()) {
        std::size_t nameEntry = static_cast<std::size_t>(objectNameOffset) + 12;
        for (std::uint32_t i = 0; i < objectNameCount && nameEntry + 12 <= b.size(); ++i, nameEntry += 12) {
            objectNames.push_back(cstr(b, u32(b, nameEntry + 8)));
        }
    }

    p = skeletonOffset;
    for (std::uint32_t i = 0; i < skeletonCount && p + 100 <= b.size(); ++i, p += 100) {
        Bone bone;
        bone.parent = s16(b, p + 4);
        bone.bindScale = {f32(b, p + 8), f32(b, p + 12), f32(b, p + 16)};
        bone.bindRotation = {f32(b, p + 20), f32(b, p + 24), f32(b, p + 28)};
        bone.bindTranslation = {f32(b, p + 32), f32(b, p + 36), f32(b, p + 40)};
        bone.name = cstr(b, u32(b, p + 92));
        out.boneIndex_[bone.name] = out.bones_.size();
        out.bones_.push_back(std::move(bone));
    }

    for (std::size_t i = 0; i < out.bones_.size(); ++i) {
        const auto local = trs(out.bones_[i].bindTranslation,
                               euler(out.bones_[i].bindRotation),
                               out.bones_[i].bindScale);
        if (out.bones_[i].parent >= 0 && static_cast<std::size_t>(out.bones_[i].parent) < i) {
            out.bones_[i].bindGlobal = mul(out.bones_[static_cast<std::size_t>(out.bones_[i].parent)].bindGlobal, local);
        } else {
            out.bones_[i].bindGlobal = local;
        }
        out.bones_[i].inverseBind = inverseAffine(out.bones_[i].bindGlobal);
    }

    struct ObjectEntry {
        std::uint16_t material = 0;
        std::uint16_t flags = 0;
        std::uint16_t node = 0;
        std::uint16_t priority = 0;
        std::uint32_t vertexCommands = 0;
        std::uint32_t vertexWords = 0;
        std::uint32_t facesOffset = 0;
        std::uint32_t facesCount = 0;
    };

    std::vector<ObjectEntry> objects;
    objects.reserve(objectCount);

    constexpr std::size_t kObjectStride = 0x38;
    for (std::uint32_t i = 0; i < objectCount; ++i) {
        const std::size_t q = static_cast<std::size_t>(objectsOffset) + static_cast<std::size_t>(i) * kObjectStride;
        if (q + kObjectStride > b.size()) break;
        objects.push_back({
            u16(b, q), u16(b, q + 2), u16(b, q + 4), u16(b, q + 6),
            u32(b, q + 8), u32(b, q + 12), u32(b, q + 16), u32(b, q + 20)
        });
    }

    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const auto& object = objects[objectIndex];
        if (h.bc != 8 && (object.flags & 1)) continue;
        if (!object.vertexCommands || object.vertexCommands >= b.size()) continue;

        Pica vertexCommands(b, object.vertexCommands, object.vertexWords);
        const auto mainPermutation = vertexCommands.mainPermutation();
        const auto bufferPermutation = vertexCommands.bufferPermutation();
        const auto formats = vertexCommands.formats();

        auto uniform6 = vertexCommands.uniform[6];
        auto uniform7 = vertexCommands.uniform[7];
        const auto pop = [](std::vector<float>& values, float fallback) {
            if (values.empty()) return fallback;
            const float value = values.back();
            values.pop_back();
            return value;
        };

        Vec4 positionOffset{
            pop(uniform6, 0), pop(uniform6, 0), pop(uniform6, 0), pop(uniform6, 0)
        };
        const float texture0Scale = pop(uniform7, 1);
        const float texture1Scale = pop(uniform7, 1);
        const float texture2Scale = pop(uniform7, 1);
        const float boneWeightScale = pop(uniform7, 1);
        const float positionScale = pop(uniform7, 1);
        const float normalScale = pop(uniform7, 1);
        const float tangentScale = pop(uniform7, 1);
        const float colorScale = pop(uniform7, 1);
        (void)texture1Scale;
        (void)texture2Scale;
        (void)tangentScale;
        (void)colorScale;

        Mesh mesh;
        mesh.materialId = object.material;
        mesh.name = object.node < objectNames.size()
            ? objectNames[object.node]
            : "mesh" + std::to_string(objectIndex);

        for (std::uint32_t faceIndex = 0; faceIndex < object.facesCount; ++faceIndex) {
            const std::size_t faceBase = static_cast<std::size_t>(object.facesOffset) + faceIndex * 0x34;
            if (faceBase + 0x34 > b.size()) break;

            const auto skinningMode = u16(b, faceBase);
            const auto nodeCount = u16(b, faceBase + 2);
            std::vector<std::uint16_t> nodes;
            nodes.reserve(nodeCount);
            for (std::uint16_t n = 0; n < nodeCount; ++n) {
                const std::size_t no = faceBase + 4 + static_cast<std::size_t>(n) * 2;
                if (no + 2 > faceBase + 0x2c) break;
                nodes.push_back(u16(b, no));
            }

            const auto faceCommandsOffset = u32(b, faceBase + 0x2c);
            const auto faceCommandsWords = u32(b, faceBase + 0x30);
            if (!faceCommandsOffset || faceCommandsOffset >= b.size()) continue;
            Pica indexCommands(b, faceCommandsOffset, faceCommandsWords);

            const auto indexCount = std::min<std::uint32_t>(indexCommands.indexCount(), 0x40000);
            const auto indexAddress = indexCommands.indexAddress();
            if (!indexAddress || indexAddress >= b.size()) continue;

            for (std::uint32_t ii = 0; ii < indexCount; ++ii) {
                const std::size_t io = static_cast<std::size_t>(indexAddress) +
                    static_cast<std::size_t>(ii) * (indexCommands.index16() ? 2 : 1);
                if (io + (indexCommands.index16() ? 2 : 1) > b.size()) break;
                const auto index = indexCommands.index16() ? u16(b, io) : b[io];

                const int stride = vertexCommands.stride();
                if (stride <= 0) continue;
                std::size_t vp = static_cast<std::size_t>(vertexCommands.vertexAddress()) +
                                 static_cast<std::size_t>(index) * static_cast<std::size_t>(stride);
                if (vp >= b.size()) continue;

                Vertex vertex;
                std::vector<float> boneIndices;
                std::vector<float> boneWeights;
                const int attributeCount = std::min(vertexCommands.totalAttributes(), 23);

                for (int attribute = 0; attribute < attributeCount; ++attribute) {
                    const int slot = bufferPermutation[static_cast<std::size_t>(attribute)];
                    if (slot < 0 || slot >= 23) continue;
                    const int semantic = mainPermutation[static_cast<std::size_t>(slot)];
                    auto format = formats[static_cast<std::size_t>(slot)];
                    if (semantic == 8) format.first = 1;
                    const int componentCount = format.second + 1;

                    const auto values = readAttribute(b, vp, format.first, componentCount);
                    if (semantic == 0 && values.size() >= 3) {
                        vertex.position = {
                            values[0] * positionScale + positionOffset.x,
                            values[1] * positionScale + positionOffset.y,
                            values[2] * positionScale + positionOffset.z
                        };
                    } else if (semantic == 1 && values.size() >= 3) {
                        vertex.normal = {values[0] * normalScale, values[1] * normalScale, values[2] * normalScale};
                    } else if (semantic == 4 && values.size() >= 2) {
                        vertex.uv = {values[0] * texture0Scale, values[1] * texture0Scale};
                    } else if (semantic == 7) {
                        boneIndices = values;
                    } else if (semantic == 8) {
                        boneWeights = values;
                    }
                }

                const std::size_t maximumInfluences = skinningMode == 1 ? 4 : 1;
                for (std::size_t k = 0; k < boneIndices.size() && k < maximumInfluences; ++k) {
                    const int localBone = static_cast<int>(boneIndices[k]);
                    if (localBone >= 0 && static_cast<std::size_t>(localBone) < nodes.size() &&
                        vertex.influenceCount < 4) {
                        vertex.bones[vertex.influenceCount++] = nodes[static_cast<std::size_t>(localBone)];
                    }
                }

                if (vertex.influenceCount == 0 && !nodes.empty() && nodes.size() <= 4) {
                    const std::size_t count = skinningMode == 1 ? nodes.size() : 1;
                    for (std::size_t k = 0; k < count && vertex.influenceCount < 4; ++k) {
                        vertex.bones[vertex.influenceCount++] = nodes[k];
                    }
                }

                for (std::size_t k = 0; k < boneWeights.size() && k < vertex.influenceCount; ++k) {
                    vertex.weights[k] = boneWeights[k] * boneWeightScale;
                }
                if (vertex.influenceCount && boneWeights.empty()) vertex.weights[0] = 1.0f;

                if (skinningMode != 1 && vertex.influenceCount) {
                    const auto bone = vertex.bones[0];
                    if (bone < out.bones_.size()) {
                        vertex.position = transformPoint(out.bones_[bone].bindGlobal, vertex.position);
                    }
                }
                mesh.vertices.push_back(vertex);
            }
        }

        if (!mesh.vertices.empty()) out.meshes_.push_back(std::move(mesh));
    }

    if (!animRaw.empty()) {
        Bytes animationBytes = animRaw;
        const auto ah = relocate(animationBytes);
        if (ah.main + 26 * 4 <= animationBytes.size()) {
            const auto animationTable = u32(animationBytes, ah.main + 24 * 4);
            const auto animationCount = std::min<std::uint32_t>(u32(animationBytes, ah.main + 25 * 4), 4096);

            for (std::uint32_t ai = 0; ai < animationCount; ++ai) {
                const std::size_t tableOffset = static_cast<std::size_t>(animationTable) + ai * 4;
                if (tableOffset + 4 > animationBytes.size()) break;
                const auto animationOffset = u32(animationBytes, tableOffset);
                if (!animationOffset || static_cast<std::size_t>(animationOffset) + 20 > animationBytes.size()) continue;

                Animation animation;
                animation.name = cstr(animationBytes, u32(animationBytes, animationOffset));
                animation.loop = (u32(animationBytes, animationOffset + 4) & 1) != 0;
                animation.frames = f32(animationBytes, animationOffset + 8);
                const auto boneTable = u32(animationBytes, animationOffset + 12);
                const auto boneCount = std::min<std::uint32_t>(u32(animationBytes, animationOffset + 16), 1024);

                for (std::uint32_t ti = 0; ti < boneCount; ++ti) {
                    const std::size_t boTable = static_cast<std::size_t>(boneTable) + ti * 4;
                    if (boTable + 4 > animationBytes.size()) break;
                    const auto boneOffset = u32(animationBytes, boTable);
                    if (!boneOffset || static_cast<std::size_t>(boneOffset) + 24 > animationBytes.size()) continue;

                    const auto type = u32(animationBytes, boneOffset + 4);
                    const auto flags = u32(animationBytes, boneOffset + 8);
                    if (((type >> 16) & 0xf) != 7) continue;

                    BoneTrack track;
                    track.boneName = cstr(animationBytes, u32(animationBytes, boneOffset));
                    const auto scaleOffset = u32(animationBytes, boneOffset + 12);
                    const auto rotationOffset = u32(animationBytes, boneOffset + 16);
                    const auto translationOffset = u32(animationBytes, boneOffset + 20);

                    const auto read3 = [&](std::uint32_t offset, std::uint32_t absentMask,
                                           std::uint32_t constantMask, Vec3Track& target) {
                        if ((flags & absentMask) || !offset || static_cast<std::size_t>(offset) + 20 > animationBytes.size()) return;
                        target.present = true;
                        if (flags & constantMask) {
                            target.constant = true;
                            target.value = {f32(animationBytes, offset), f32(animationBytes, offset + 4), f32(animationBytes, offset + 8)};
                        } else {
                            target.start = f32(animationBytes, offset);
                            target.end = f32(animationBytes, offset + 4);
                            const auto dataOffset = u32(animationBytes, offset + 12);
                            const auto entries = std::min<std::uint32_t>(u32(animationBytes, offset + 16), 0x4000);
                            for (std::uint32_t k = 0; k < entries; ++k) {
                                const std::size_t so = static_cast<std::size_t>(dataOffset) + k * 12;
                                if (so + 12 > animationBytes.size()) break;
                                target.samples.push_back({f32(animationBytes, so), f32(animationBytes, so + 4), f32(animationBytes, so + 8)});
                            }
                        }
                    };

                    read3(scaleOffset, 0x20, 4, track.scale);
                    read3(translationOffset, 8, 1, track.translation);

                    if (!(flags & 0x10) && rotationOffset && static_cast<std::size_t>(rotationOffset) + 20 <= animationBytes.size()) {
                        track.rotation.present = true;
                        if (flags & 2) {
                            track.rotation.constant = true;
                            track.rotation.value = {
                                f32(animationBytes, rotationOffset), f32(animationBytes, rotationOffset + 4),
                                f32(animationBytes, rotationOffset + 8), f32(animationBytes, rotationOffset + 12)
                            };
                        } else {
                            track.rotation.start = f32(animationBytes, rotationOffset);
                            track.rotation.end = f32(animationBytes, rotationOffset + 4);
                            const auto dataOffset = u32(animationBytes, rotationOffset + 12);
                            const auto entries = std::min<std::uint32_t>(u32(animationBytes, rotationOffset + 16), 0x4000);
                            for (std::uint32_t k = 0; k < entries; ++k) {
                                const std::size_t qo = static_cast<std::size_t>(dataOffset) + k * 16;
                                if (qo + 16 > animationBytes.size()) break;
                                track.rotation.samples.push_back({
                                    f32(animationBytes, qo), f32(animationBytes, qo + 4),
                                    f32(animationBytes, qo + 8), f32(animationBytes, qo + 12)
                                });
                            }
                        }
                    }
                    animation.tracks.push_back(std::move(track));
                }
                out.animations_.push_back(std::move(animation));
            }
        }
    }

    return out;
}

std::vector<Mat4> Scene::samplePose(const Animation* animation, float frame) const {
    std::vector<Vec3> scales;
    std::vector<Vec3> translations;
    std::vector<Quat> rotations;
    scales.reserve(bones_.size());
    translations.reserve(bones_.size());
    rotations.reserve(bones_.size());

    for (const auto& bone : bones_) {
        scales.push_back(bone.bindScale);
        translations.push_back(bone.bindTranslation);
        rotations.push_back(euler(bone.bindRotation));
    }

    if (animation) {
        for (const auto& track : animation->tracks) {
            const auto it = boneIndex_.find(track.boneName);
            if (it == boneIndex_.end()) continue;
            const auto index = it->second;

            if (track.scale.present) {
                scales[index] = track.scale.constant
                    ? track.scale.value
                    : sample(track.scale.samples, track.scale.start, track.scale.end, frame, scales[index],
                             [](Vec3 a, Vec3 b, float t) { return lerp(a, b, t); });
            }
            if (track.translation.present) {
                translations[index] = track.translation.constant
                    ? track.translation.value
                    : sample(track.translation.samples, track.translation.start, track.translation.end, frame, translations[index],
                             [](Vec3 a, Vec3 b, float t) { return lerp(a, b, t); });
            }
            if (track.rotation.present) {
                rotations[index] = track.rotation.constant
                    ? track.rotation.value
                    : sample(track.rotation.samples, track.rotation.start, track.rotation.end, frame, rotations[index],
                             [](Quat a, Quat b, float t) { return slerp(a, b, t); });
            }
        }
    }

    std::vector<Mat4> globals(bones_.size());
    std::vector<Mat4> skinMatrices(bones_.size());
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        const auto local = trs(translations[i], rotations[i], scales[i]);
        const int parent = bones_[i].parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < i) {
            globals[i] = mul(globals[static_cast<std::size_t>(parent)], local);
        } else {
            globals[i] = local;
        }
        skinMatrices[i] = mul(globals[i], bones_[i].inverseBind);
    }
    return skinMatrices;
}

Vec3 Scene::skin(const Vertex& vertex, const std::vector<Mat4>& matrices) const {
    if (!vertex.influenceCount) return vertex.position;

    Vec3 out{};
    float sum = 0;
    for (int i = 0; i < vertex.influenceCount; ++i) {
        float weight = vertex.weights[static_cast<std::size_t>(i)];
        if (weight == 0 && i == 0) weight = 1;
        const auto bone = vertex.bones[static_cast<std::size_t>(i)];
        if (bone >= matrices.size()) continue;
        const auto point = transformPoint(matrices[bone], vertex.position);
        out.x += point.x * weight;
        out.y += point.y * weight;
        out.z += point.z * weight;
        sum += weight;
    }

    if (sum > 1e-5f && std::fabs(sum - 1.0f) > 1e-3f) {
        out.x /= sum;
        out.y /= sum;
        out.z /= sum;
    }
    return sum > 1e-5f ? out : vertex.position;
}

} // namespace promptemon::bch
