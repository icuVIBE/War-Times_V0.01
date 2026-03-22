#include "BitStream.h"
#include <cmath>
#include <algorithm>

namespace WT {

using namespace DirectX;

// ============================================================
// Constructor (read mode)
// ============================================================

BitStream::BitStream(const uint8_t* data, int sizeBytes) {
    m_buffer.assign(data, data + sizeBytes);
    m_totalBits = sizeBytes * 8;
    m_bitPosition = m_totalBits;
    m_readBitPos = 0;
    m_overflow = false;
}

void BitStream::Reset() {
    m_buffer.clear();
    m_bitPosition = 0;
    m_readBitPos = 0;
    m_totalBits = 0;
    m_overflow = false;
}

// ============================================================
// Capacity
// ============================================================

void BitStream::EnsureCapacity(int bitsNeeded) {
    int bytesNeeded = (m_bitPosition + bitsNeeded + 7) / 8;
    if (bytesNeeded > static_cast<int>(m_buffer.size())) {
        m_buffer.resize(bytesNeeded + 64, 0);
    }
}

int BitStream::BitsRequired(int range) {
    if (range <= 0) return 0;
    int bits = 0;
    unsigned val = static_cast<unsigned>(range);
    while (val > 0) { val >>= 1; bits++; }
    return bits;
}

// ============================================================
// Bit-level write
// ============================================================

void BitStream::WriteBit(bool value) {
    EnsureCapacity(1);
    int byteIndex = m_bitPosition / 8;
    int bitIndex = m_bitPosition % 8;
    if (value) {
        m_buffer[byteIndex] |= (1 << bitIndex);
    } else {
        m_buffer[byteIndex] &= ~(1 << bitIndex);
    }
    m_bitPosition++;
}

void BitStream::WriteBits(uint32_t value, int bitCount) {
    EnsureCapacity(bitCount);
    for (int i = 0; i < bitCount; i++) {
        WriteBit((value >> i) & 1);
    }
}

void BitStream::WriteBytes(const void* data, int byteCount) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < byteCount; i++) {
        WriteBits(bytes[i], 8);
    }
}

// ============================================================
// Typed writers
// ============================================================

void BitStream::WriteUInt32(uint32_t v) {
    WriteBits(v & 0xFFFF, 16);
    WriteBits(v >> 16, 16);
}

void BitStream::WriteInt32(int32_t v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    WriteUInt32(u);
}

void BitStream::WriteFloat(float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    WriteUInt32(u);
}

void BitStream::WriteFloat16(float v) {
    // Simple half-float: sign(1) + exponent(5) + mantissa(10)
    uint32_t f32;
    memcpy(&f32, &v, 4);

    uint32_t sign = (f32 >> 31) & 1;
    int32_t exp = ((f32 >> 23) & 0xFF) - 127;
    uint32_t mant = f32 & 0x7FFFFF;

    uint16_t f16;
    if (exp > 15) {
        f16 = static_cast<uint16_t>((sign << 15) | (0x1F << 10)); // Inf
    } else if (exp < -14) {
        f16 = static_cast<uint16_t>(sign << 15); // Zero
    } else {
        f16 = static_cast<uint16_t>((sign << 15) | ((exp + 15) << 10) | (mant >> 13));
    }
    WriteBits(f16, 16);
}

void BitStream::WriteString(const std::string& str) {
    int len = static_cast<int>(str.size());
    if (len > 255) len = 255;
    WriteUInt8(static_cast<uint8_t>(len));
    WriteBytes(str.c_str(), len);
}

void BitStream::WriteVec3(const XMFLOAT3& v) {
    WriteFloat(v.x);
    WriteFloat(v.y);
    WriteFloat(v.z);
}

void BitStream::WriteCompressedVec3(const XMFLOAT3& v, float range, int bitsPerAxis) {
    int maxVal = (1 << bitsPerAxis) - 1;
    float invRange = maxVal / (range * 2.0f);

    int qx = static_cast<int>((v.x + range) * invRange);
    int qy = static_cast<int>((v.y + range) * invRange);
    int qz = static_cast<int>((v.z + range) * invRange);

    qx = (std::max)(0, (std::min)(maxVal, qx));
    qy = (std::max)(0, (std::min)(maxVal, qy));
    qz = (std::max)(0, (std::min)(maxVal, qz));

    WriteBits(static_cast<uint32_t>(qx), bitsPerAxis);
    WriteBits(static_cast<uint32_t>(qy), bitsPerAxis);
    WriteBits(static_cast<uint32_t>(qz), bitsPerAxis);
}

void BitStream::WriteRanged(int value, int minVal, int maxVal) {
    int range = maxVal - minVal;
    int bits = BitsRequired(range);
    WriteBits(static_cast<uint32_t>(value - minVal), bits);
}

// ============================================================
// Bit-level read
// ============================================================

bool BitStream::ReadBit() {
    if (m_readBitPos >= m_totalBits) { m_overflow = true; return false; }
    int byteIndex = m_readBitPos / 8;
    int bitIndex = m_readBitPos % 8;
    m_readBitPos++;
    return (m_buffer[byteIndex] >> bitIndex) & 1;
}

uint32_t BitStream::ReadBits(int bitCount) {
    uint32_t value = 0;
    for (int i = 0; i < bitCount; i++) {
        if (ReadBit()) value |= (1u << i);
    }
    return value;
}

void BitStream::ReadBytes(void* data, int byteCount) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (int i = 0; i < byteCount; i++) {
        bytes[i] = static_cast<uint8_t>(ReadBits(8));
    }
}

// ============================================================
// Typed readers
// ============================================================

uint32_t BitStream::ReadUInt32() {
    uint32_t lo = ReadBits(16);
    uint32_t hi = ReadBits(16);
    return lo | (hi << 16);
}

int32_t BitStream::ReadInt32() {
    uint32_t u = ReadUInt32();
    int32_t v;
    memcpy(&v, &u, 4);
    return v;
}

float BitStream::ReadFloat() {
    uint32_t u = ReadUInt32();
    float v;
    memcpy(&v, &u, 4);
    return v;
}

float BitStream::ReadFloat16() {
    uint16_t f16 = static_cast<uint16_t>(ReadBits(16));

    uint32_t sign = (f16 >> 15) & 1;
    uint32_t exp = (f16 >> 10) & 0x1F;
    uint32_t mant = f16 & 0x3FF;

    uint32_t f32;
    if (exp == 0) {
        f32 = sign << 31; // Zero
    } else if (exp == 0x1F) {
        f32 = (sign << 31) | (0xFF << 23); // Inf
    } else {
        f32 = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
    }

    float v;
    memcpy(&v, &f32, 4);
    return v;
}

std::string BitStream::ReadString() {
    uint8_t len = ReadUInt8();
    std::string str(len, '\0');
    ReadBytes(str.data(), len);
    return str;
}

XMFLOAT3 BitStream::ReadVec3() {
    float x = ReadFloat();
    float y = ReadFloat();
    float z = ReadFloat();
    return { x, y, z };
}

XMFLOAT3 BitStream::ReadCompressedVec3(float range, int bitsPerAxis) {
    int maxVal = (1 << bitsPerAxis) - 1;
    float scale = (range * 2.0f) / maxVal;

    int qx = static_cast<int>(ReadBits(bitsPerAxis));
    int qy = static_cast<int>(ReadBits(bitsPerAxis));
    int qz = static_cast<int>(ReadBits(bitsPerAxis));

    return {
        qx * scale - range,
        qy * scale - range,
        qz * scale - range
    };
}

int BitStream::ReadRanged(int minVal, int maxVal) {
    int range = maxVal - minVal;
    int bits = BitsRequired(range);
    return static_cast<int>(ReadBits(bits)) + minVal;
}

} // namespace WT
