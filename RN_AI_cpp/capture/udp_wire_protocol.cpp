#include "udp_wire_protocol.h"
#include <cstring>
#include <algorithm>

// Helper functions for little-endian encoding/decoding
static void write_u16_le(uint8_t* buffer, uint16_t value) {
    buffer[0] = static_cast<uint8_t>(value & 0xFF);
    buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

static void write_u32_le(uint8_t* buffer, uint32_t value) {
    buffer[0] = static_cast<uint8_t>(value & 0xFF);
    buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static void write_u64_le(uint8_t* buffer, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buffer[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

static uint16_t read_u16_le(const uint8_t* buffer) {
    return static_cast<uint16_t>(buffer[0]) |
           (static_cast<uint16_t>(buffer[1]) << 8);
}

static uint32_t read_u32_le(const uint8_t* buffer) {
    return static_cast<uint32_t>(buffer[0]) |
           (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
}

static uint64_t read_u64_le(const uint8_t* buffer) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(buffer[i]) << (i * 8);
    }
    return result;
}

// FrameHeader implementation
void FrameHeader::encode(uint8_t* buffer) const {
    buffer[0] = version;
    write_u32_le(buffer + 1, width);
    write_u32_le(buffer + 5, height);
    write_u64_le(buffer + 9, frame_seq);
    write_u64_le(buffer + 17, capture_timestamp_ms);
    buffer[25] = format;
    write_u32_le(buffer + 26, jpeg_size);
}

std::optional<FrameHeader> FrameHeader::decode(const uint8_t* buffer, size_t size) {
    if (size < FRAME_HEADER_SIZE) {
        return std::nullopt;
    }

    FrameHeader header;
    header.version = buffer[0];
    header.width = read_u32_le(buffer + 1);
    header.height = read_u32_le(buffer + 5);
    header.frame_seq = read_u64_le(buffer + 9);
    header.capture_timestamp_ms = read_u64_le(buffer + 17);
    header.format = buffer[25];
    header.jpeg_size = read_u32_le(buffer + 26);

    return header;
}

// Length-prefixed frame encoding/decoding
void encode_length_prefixed(
    const FrameHeader& header,
    const uint8_t* jpeg_data,
    size_t jpeg_size,
    std::vector<uint8_t>& output
) {
    // Calculate total size: 4 (payload_len) + FRAME_HEADER_SIZE (header) + jpeg_size
    size_t total_size = 4 + FRAME_HEADER_SIZE + jpeg_size;
    output.resize(total_size);

    // Write payload length (header + jpeg)
    uint32_t payload_len = static_cast<uint32_t>(FRAME_HEADER_SIZE + jpeg_size);
    write_u32_le(output.data(), payload_len);

    // Write header
    header.encode(output.data() + 4);

    // Write JPEG data
    if (jpeg_size > 0 && jpeg_data != nullptr) {
        std::memcpy(output.data() + 4 + FRAME_HEADER_SIZE, jpeg_data, jpeg_size);
    }
}

std::optional<DecodedFrame> decode_length_prefixed(const uint8_t* data, size_t size) {
    if (size < 4 + FRAME_HEADER_SIZE) {
        return std::nullopt;
    }

    // Read payload length
    uint32_t payload_len = read_u32_le(data);

    // Verify we have enough data
    if (size < 4 + payload_len) {
        return std::nullopt;
    }

    // Decode header
    auto header_opt = FrameHeader::decode(data + 4, payload_len);
    if (!header_opt) {
        return std::nullopt;
    }

    DecodedFrame frame;
    frame.header = *header_opt;
    frame.jpeg_data = data + 4 + FRAME_HEADER_SIZE;
    frame.jpeg_size = payload_len - FRAME_HEADER_SIZE;

    return frame;
}

// FragmentEncoder implementation
void FragmentEncoder::encode(
    uint16_t frame_id,
    const uint8_t* data,
    size_t data_size,
    std::vector<std::vector<uint8_t>>& datagrams
) {
    datagrams.clear();

    if (data_size == 0) {
        return;
    }

    // Calculate number of fragments needed
    uint8_t frag_count = static_cast<uint8_t>((data_size + MAX_FRAG_PAYLOAD - 1) / MAX_FRAG_PAYLOAD);

    size_t offset = 0;
    for (uint8_t frag_idx = 0; frag_idx < frag_count; ++frag_idx) {
        // Calculate fragment data size
        size_t remaining = data_size - offset;
        size_t frag_data_size = std::min(remaining, MAX_FRAG_PAYLOAD);

        // Create datagram
        std::vector<uint8_t> datagram(FRAG_HEADER_SIZE + frag_data_size);

        // Write fragment header
        // Magic "SWTF"
        datagram[0] = 'S';
        datagram[1] = 'W';
        datagram[2] = 'T';
        datagram[3] = 'F';

        // Version
        datagram[4] = PROTOCOL_VERSION;

        // Header length
        datagram[5] = FRAG_HEADER_SIZE;

        // Frame ID (u16 LE)
        write_u16_le(datagram.data() + 6, frame_id);

        // Fragment count
        datagram[8] = frag_count;

        // Fragment index
        datagram[9] = frag_idx;

        // Copy fragment data
        if (frag_data_size > 0) {
            std::memcpy(datagram.data() + FRAG_HEADER_SIZE, data + offset, frag_data_size);
        }

        datagrams.push_back(std::move(datagram));
        offset += frag_data_size;
    }
}

std::optional<FragmentEncoder::FragmentInfo> FragmentEncoder::decode_datagram(
    const uint8_t* datagram,
    size_t size
) {
    // Minimum size check
    if (size < FRAG_HEADER_SIZE) {
        return std::nullopt;
    }

    // Verify magic "SWTF"
    if (datagram[0] != 'S' || datagram[1] != 'W' ||
        datagram[2] != 'T' || datagram[3] != 'F') {
        return std::nullopt;
    }

    // Verify version
    if (datagram[4] != PROTOCOL_VERSION) {
        return std::nullopt;
    }

    // Verify header length
    if (datagram[5] != FRAG_HEADER_SIZE) {
        return std::nullopt;
    }

    FragmentInfo info;
    info.frame_id = read_u16_le(datagram + 6);
    info.frag_count = datagram[8];
    info.frag_idx = datagram[9];
    info.data = datagram + FRAG_HEADER_SIZE;
    info.data_size = size - FRAG_HEADER_SIZE;

    return info;
}

// FragmentAssembler implementation
FragmentAssembler::FragmentAssembler(uint16_t frame_id, uint8_t frag_count)
    : m_frame_id(frame_id)
    , m_frag_count(frag_count)
    , m_fragments(frag_count)
    , m_received_count(0)
{
}

bool FragmentAssembler::add_fragment(uint8_t frag_idx, const uint8_t* data, size_t data_size) {
    // Check if fragment index is valid
    if (frag_idx >= m_frag_count) {
        return false;
    }

    // Check if we already have this fragment
    if (m_fragments[frag_idx].has_value()) {
        return false;
    }

    // Store fragment
    m_fragments[frag_idx] = std::vector<uint8_t>(data, data + data_size);
    ++m_received_count;

    return true;
}

bool FragmentAssembler::is_complete() const {
    return m_received_count == m_frag_count;
}

std::vector<uint8_t> FragmentAssembler::assemble() const {
    if (!is_complete()) {
        return {};
    }

    // Calculate total size
    size_t total_size = 0;
    for (const auto& frag : m_fragments) {
        if (frag.has_value()) {
            total_size += frag->size();
        }
    }

    // Assemble fragments
    std::vector<uint8_t> result;
    result.reserve(total_size);

    for (const auto& frag : m_fragments) {
        if (frag.has_value()) {
            result.insert(result.end(), frag->begin(), frag->end());
        }
    }

    return result;
}

// Frame ID wraparound handling
bool is_new_frame_id(uint16_t new_id, uint16_t old_id) {
    // Handle wraparound: if the difference is more than half the range,
    // consider it as wrapping around
    const uint16_t half_range = 0x8000; // 32768

    if (new_id == old_id) {
        return false; // Same ID, not newer
    }

    int32_t diff = static_cast<int32_t>(new_id) - static_cast<int32_t>(old_id);

    // Handle wraparound
    if (diff < 0) {
        diff += 0x10000; // Add 2^16 for wraparound
    }

    // If the difference is less than half the range, consider it newer
    return diff > 0 && diff < half_range;
}
