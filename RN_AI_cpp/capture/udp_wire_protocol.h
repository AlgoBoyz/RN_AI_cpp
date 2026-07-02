#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <string>
#include <array>

// Constants
constexpr size_t FRAME_HEADER_SIZE = 30;
constexpr size_t FRAG_HEADER_SIZE = 10;
constexpr size_t MAX_DATAGRAM_SIZE = 1400;
constexpr size_t MAX_FRAG_PAYLOAD = MAX_DATAGRAM_SIZE - FRAG_HEADER_SIZE;
constexpr size_t MAX_FRAGMENTS_PER_FRAME = 255;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t FORMAT_JPEG = 1;

// Frame header structure (22 bytes)
struct FrameHeader {
    uint8_t version = PROTOCOL_VERSION;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frame_seq = 0;
    uint64_t capture_timestamp_ms = 0;
    uint8_t format = FORMAT_JPEG;
    uint32_t jpeg_size = 0;

    // Encode to buffer (must be at least FRAME_HEADER_SIZE bytes)
    void encode(uint8_t* buffer) const;

    // Decode from buffer (must be at least FRAME_HEADER_SIZE bytes)
    static std::optional<FrameHeader> decode(const uint8_t* buffer, size_t size);
};

// Encode frame as length-prefixed format: [4B payload_len][22B header][JPEG data]
void encode_length_prefixed(
    const FrameHeader& header,
    const uint8_t* jpeg_data,
    size_t jpeg_size,
    std::vector<uint8_t>& output
);

// Decode length-prefixed frame, returns header and pointer to JPEG data
// Returns nullptr on failure
struct DecodedFrame {
    FrameHeader header;
    const uint8_t* jpeg_data;
    size_t jpeg_size;
};
std::optional<DecodedFrame> decode_length_prefixed(const uint8_t* data, size_t size);

// Fragment encoder - splits complete frame into UDP datagrams
class FragmentEncoder {
public:
    // Encode complete frame data into fragments
    // frame_id: 16-bit frame identifier (wraps around)
    // data: complete frame data (length-prefixed format)
    // datagrams: output vector of datagram buffers
    static void encode(
        uint16_t frame_id,
        const uint8_t* data,
        size_t data_size,
        std::vector<std::vector<uint8_t>>& datagrams
    );

    // Decode a single datagram, returns fragment info
    // Returns nullopt if datagram is invalid
    struct FragmentInfo {
        uint16_t frame_id;
        uint8_t frag_count;
        uint8_t frag_idx;
        const uint8_t* data;
        size_t data_size;
    };
    static std::optional<FragmentInfo> decode_datagram(const uint8_t* datagram, size_t size);
};

// Fragment assembler - reassembles fragments into complete frame
class FragmentAssembler {
public:
    FragmentAssembler(uint16_t frame_id, uint8_t frag_count);

    // Add a fragment, returns false if duplicate or invalid
    bool add_fragment(uint8_t frag_idx, const uint8_t* data, size_t data_size);

    // Check if all fragments have been received
    bool is_complete() const;

    // Assemble complete frame (only valid if is_complete() returns true)
    std::vector<uint8_t> assemble() const;

    uint16_t frame_id() const { return m_frame_id; }
    uint8_t frag_count() const { return m_frag_count; }

private:
    uint16_t m_frame_id;
    uint8_t m_frag_count;
    std::vector<std::optional<std::vector<uint8_t>>> m_fragments;
    uint8_t m_received_count = 0;
};

// Check if new_frame_id is newer than old_frame_id (handles u16 wraparound)
// Returns true if new_frame_id is considered "newer" than old_frame_id
bool is_new_frame_id(uint16_t new_id, uint16_t old_id);
