#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <DirectXMath.h>

namespace WT {

// ============================================================
// BitStream — compact bitpacked serialisation for network packets
// ============================================================
// Supports both reading and writing. Data is packed tightly at
// the bit level to minimise bandwidth. Commonly used for delta-
// compressed entity state, input commands, and small packets.

class BitStream {
public:
    // ---- Write mode ----
    BitStream() = default;

    // ---- Read mode (from received buffer) ----
    BitStream(const uint8_t* data, int sizeBytes);

    // Reset for reuse.
    void Reset();

    // ---- Bit-level write ----
    void WriteBit(bool value);
    void WriteBits(uint32_t value, int bitCount);
    void WriteBytes(const void* data, int byteCount);

    // ---- Typed writers ----
    void WriteBool(bool v)     { WriteBit(v); }
    void WriteUInt8(uint8_t v) { WriteBits(v, 8); }
    void WriteUInt16(uint16_t v) { WriteBits(v, 16); }
    void WriteUInt32(uint32_t v);
    void WriteInt32(int32_t v);
    void WriteFloat(float v);
    void WriteFloat16(float v);   // Half-precision (16-bit) float
    void WriteString(const std::string& str);
    void WriteVec3(const DirectX::XMFLOAT3& v);
    void WriteCompressedVec3(const DirectX::XMFLOAT3& v, float range, int bitsPerAxis);

    // Write a value with known range [min, max] using minimal bits.
    void WriteRanged(int value, int minVal, int maxVal);

    // ---- Bit-level read ----
    bool     ReadBit();
    uint32_t ReadBits(int bitCount);
    void     ReadBytes(void* data, int byteCount);

    // ---- Typed readers ----
    bool        ReadBool()   { return ReadBit(); }
    uint8_t     ReadUInt8()  { return static_cast<uint8_t>(ReadBits(8)); }
    uint16_t    ReadUInt16() { return static_cast<uint16_t>(ReadBits(16)); }
    uint32_t    ReadUInt32();
    int32_t     ReadInt32();
    float       ReadFloat();
    float       ReadFloat16();
    std::string ReadString();
    DirectX::XMFLOAT3 ReadVec3();
    DirectX::XMFLOAT3 ReadCompressedVec3(float range, int bitsPerAxis);
    int         ReadRanged(int minVal, int maxVal);

    // ---- Buffer access ----
    const uint8_t* GetData() const { return m_buffer.data(); }
    int GetSizeBytes() const { return (m_bitPosition + 7) / 8; }
    int GetSizeBits() const { return m_bitPosition; }
    int GetReadBitPosition() const { return m_readBitPos; }
    bool IsOverflow() const { return m_overflow; }

private:
    void EnsureCapacity(int bitsNeeded);

    // Integer needed to count bits in a range.
    static int BitsRequired(int range);

    std::vector<uint8_t> m_buffer;
    int m_bitPosition = 0;   // Write cursor (bits)
    int m_readBitPos = 0;    // Read cursor (bits)
    int m_totalBits = 0;     // Total bits in buffer (read mode)
    bool m_overflow = false;
};

} // namespace WT
