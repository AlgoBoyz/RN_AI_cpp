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
constexpr uint8_t PROTOCOL_VERSION_V1 = 1;
constexpr uint8_t PROTOCOL_VERSION_V2 = 2;  // multi-region
constexpr uint8_t FORMAT_JPEG = 1;
constexpr uint8_t FORMAT_MULTI_REGION = 2;
constexpr int MAX_REGIONS = 8;
constexpr size_t REGION_ENTRY_SIZE = 19; // 1+2+2+2+2+4+4

// Frame header structure (30 bytes) — V1 & V2 share the same wire layout.
// V1: single JPEG, width/height = image dims, jpeg_size = JPEG size.
// V2 (FORMAT_MULTI_REGION): width/height = full source dims for coordinate ref,
//     jpeg_size = total payload size after header, followed by region table + JPEGs.
struct FrameHeader {
    uint8_t version = PROTOCOL_VERSION_V1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t frame_seq = 0;
    uint64_t capture_timestamp_ms = 0;
    uint8_t format = FORMAT_JPEG;
    uint32_t jpeg_size = 0;

    void encode(uint8_t* buffer) const;
    static std::optional<FrameHeader> decode(const uint8_t* buffer, size_t size);
};

// ── V2 multi-region entries ──────────────────────────────────────────
struct RegionEntry {
    uint8_t  id = 0;         // 0 = main (AI), 1 = ammo, 2+ = future
    int32_t  src_x = 0;      // source x on full frame
    int32_t  src_y = 0;      // source y on full frame
    int32_t  width  = 0;
    int32_t  height = 0;
    uint32_t jpeg_offset = 0; // byte offset from end of region table
    uint32_t jpeg_size   = 0;

    void encode(uint8_t* buffer) const;
    static std::optional<RegionEntry> decode(const uint8_t* buffer, size_t available);
};

struct MultiRegionHeader {
    uint32_t num_regions = 0;
    RegionEntry regions[MAX_REGIONS];
};

// ── V1 helpers (unchanged) ──────────────────────────────────────────
void encode_length_prefixed(
    const FrameHeader& header,
    const uint8_t* jpeg_data,
    size_t jpeg_size,
    std::vector<uint8_t>& output
);

struct DecodedFrame {
    FrameHeader header;
    const uint8_t* jpeg_data;
    size_t jpeg_size;
};
std::optional<DecodedFrame> decode_length_prefixed(const uint8_t* data, size_t size);

// ── V2 multi-region helpers ──────────────────────────────────────────
// Payload layout after 30B FrameHeader:
//   [4B num_regions]
//   [REGION_ENTRY_SIZE B * num_regions]
//   [JPEG data for region 0]
//   [JPEG data for region 1]
//   ...
void encode_multi_region(
    const FrameHeader& header,
    const MultiRegionHeader& mr_header,
    const std::vector<std::vector<uint8_t>>& region_jpegs,
    std::vector<uint8_t>& output
);

struct DecodedMultiRegion {
    FrameHeader header;
    MultiRegionHeader mr_header;
    // Pointers into the decoded buffer; valid as long as the buffer lives.
    const uint8_t* region_data[MAX_REGIONS] = {};
    size_t         region_size[MAX_REGIONS] = {};
};
std::optional<DecodedMultiRegion> decode_multi_region(const uint8_t* data, size_t size);

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
    uint8_t received_count() const { return m_received_count; }

private:
    uint16_t m_frame_id;
    uint8_t m_frag_count;
    std::vector<std::optional<std::vector<uint8_t>>> m_fragments;
    uint8_t m_received_count = 0;
};

// Check if new_frame_id is newer than old_frame_id (handles u16 wraparound)
// Returns true if new_frame_id is considered "newer" than old_frame_id
bool is_new_frame_id(uint16_t new_id, uint16_t old_id);
