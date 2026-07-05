#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iostream>

#include <libgpujpeg/gpujpeg.h>
#include <libgpujpeg/gpujpeg_encoder.h>
#include <libgpujpeg/gpujpeg_common.h>

#include "udp_wire_protocol.h"

// ============================================================
// RawImage — lightweight image buffer (replaces cv::Mat)
// ============================================================
struct RawImage {
    uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    bool owns_data = false;

    RawImage() = default;

    RawImage(int w, int h, int ch)
        : width(w), height(h), channels(ch), owns_data(true) {
        data = new uint8_t[static_cast<size_t>(w) * h * ch]();
    }

    ~RawImage() {
        if (owns_data) delete[] data;
    }

    RawImage(const RawImage&) = delete;
    RawImage& operator=(const RawImage&) = delete;

    RawImage(RawImage&& other) noexcept
        : data(other.data), width(other.width), height(other.height),
          channels(other.channels), owns_data(other.owns_data) {
        other.data = nullptr;
        other.width = 0;
        other.height = 0;
        other.channels = 0;
        other.owns_data = false;
    }

    RawImage& operator=(RawImage&& other) noexcept {
        if (this != &other) {
            if (owns_data) delete[] data;
            data = other.data;
            width = other.width;
            height = other.height;
            channels = other.channels;
            owns_data = other.owns_data;
            other.data = nullptr;
            other.width = 0;
            other.height = 0;
            other.channels = 0;
            other.owns_data = false;
        }
        return *this;
    }

    bool empty() const { return !data || width == 0 || height == 0; }
    size_t total_bytes() const { return static_cast<size_t>(width) * height * channels; }
};

// BGRA → RGB (one pass, drops alpha)
static void bgra_to_rgb(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + y * width * 4;
        uint8_t* dst_row = dst + y * width * 3;
        for (int x = 0; x < width; x++) {
            dst_row[x * 3 + 0] = src_row[x * 4 + 2]; // R
            dst_row[x * 3 + 1] = src_row[x * 4 + 1]; // G
            dst_row[x * 3 + 2] = src_row[x * 4 + 0]; // B
        }
    }
}

// Center crop a BGRA RawImage to a square
static RawImage center_crop_bgra(const RawImage& src, int crop_size) {
    if (src.width <= crop_size || src.height <= crop_size) {
        RawImage result(src.width, src.height, 4);
        memcpy(result.data, src.data, result.total_bytes());
        return result;
    }
    int x = (src.width - crop_size) / 2;
    int y = (src.height - crop_size) / 2;
    RawImage result(crop_size, crop_size, 4);
    int src_stride = src.width * 4;
    int dst_stride = crop_size * 4;
    for (int row = 0; row < crop_size; row++) {
        memcpy(result.data + row * dst_stride,
               src.data + (y + row) * src_stride + x * 4, dst_stride);
    }
    return result;
}

// ============================================================
// DDA Screen Capture (no OpenCV)
// ============================================================
template <typename T>
static inline void SafeRelease(T** pp) {
    if (*pp) { (*pp)->Release(); *pp = nullptr; }
}

struct FrameContext {
    ID3D11Texture2D* texture = nullptr;
    bool hasAcquiredFrame = false;
};

class DDAManager {
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGIOutputDuplication* m_duplication = nullptr;
    IDXGIOutput1* m_output1 = nullptr;
    DXGI_OUTDUPL_DESC m_duplDesc{};
    bool m_frameAcquired = false;

public:
    ~DDAManager() { Release(); }

    ID3D11Device* Device() const { return m_device; }
    ID3D11DeviceContext* Context() const { return m_context; }
    IDXGIOutputDuplication* Duplication() const { return m_duplication; }

    HRESULT Initialize(int monitorIndex, int& outScreenW, int& outScreenH) {
        IDXGIFactory1* factory = nullptr;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory);
        if (FAILED(hr)) return hr;

        IDXGIAdapter1* adapter = nullptr;
        hr = factory->EnumAdapters1(monitorIndex, &adapter);
        if (FAILED(hr)) { factory->Release(); return hr; }

        IDXGIOutput* output = nullptr;
        hr = adapter->EnumOutputs(0, &output);
        if (FAILED(hr)) { SafeRelease(&adapter); SafeRelease(&factory); return hr; }

        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                               &fl, 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
        if (FAILED(hr)) { SafeRelease(&output); SafeRelease(&adapter); SafeRelease(&factory); return hr; }

        hr = output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&m_output1);
        if (FAILED(hr)) { SafeRelease(&m_device); SafeRelease(&output); SafeRelease(&adapter); SafeRelease(&factory); return hr; }

        hr = m_output1->DuplicateOutput(m_device, &m_duplication);
        if (FAILED(hr)) { SafeRelease(&m_output1); SafeRelease(&m_device); SafeRelease(&output); SafeRelease(&adapter); SafeRelease(&factory); return hr; }

        m_duplication->GetDesc(&m_duplDesc);
        DXGI_OUTPUT_DESC oDesc{};
        output->GetDesc(&oDesc);
        outScreenW = oDesc.DesktopCoordinates.right - oDesc.DesktopCoordinates.left;
        outScreenH = oDesc.DesktopCoordinates.bottom - oDesc.DesktopCoordinates.top;

        SafeRelease(&output); SafeRelease(&adapter); SafeRelease(&factory);
        return S_OK;
    }

    HRESULT AcquireFrame(FrameContext& ctx, UINT timeout = 100) {
        ctx = {};
        if (!m_duplication) return E_FAIL;
        DXGI_OUTDUPL_FRAME_INFO fi{};
        IDXGIResource* resource = nullptr;
        HRESULT hr = m_duplication->AcquireNextFrame(timeout, &fi, &resource);
        if (FAILED(hr)) return hr;
        m_frameAcquired = true;
        ctx.hasAcquiredFrame = true;
        if (resource) {
            hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&ctx.texture);
            resource->Release();
        }
        return hr;
    }

    void ReleaseFrame() {
        if (m_duplication && m_frameAcquired) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }
    }

    void Release() {
        if (m_duplication) { ReleaseFrame(); m_duplication->Release(); m_duplication = nullptr; }
        SafeRelease(&m_output1);
        SafeRelease(&m_context);
        SafeRelease(&m_device);
    }
};

class FrameCapture {
    DDAManager m_dda;
    ID3D11Texture2D* m_staging = nullptr;
    int m_regionW = 0, m_regionH = 0;
    int m_screenW = 0, m_screenH = 0;

public:
    FrameCapture(int desiredW, int desiredH, int monitorIndex) {
        m_regionW = desiredW;
        m_regionH = desiredH;
        HRESULT hr = m_dda.Initialize(monitorIndex, m_screenW, m_screenH);
        if (FAILED(hr)) {
            fprintf(stderr, "[Capture] DDA init failed hr=0x%x\n", hr);
            return;
        }
        m_regionW = std::clamp(m_regionW, 1, std::max(1, m_screenW));
        m_regionH = std::clamp(m_regionH, 1, std::max(1, m_screenH));
        createStaging();
    }

    ~FrameCapture() {
        SafeRelease(&m_staging);
    }

    bool valid() const { return m_dda.Duplication() && m_staging; }

    RawImage GetNextFrame() {
        if (!valid()) return {};

        FrameContext ctx;
        HRESULT hr = m_dda.AcquireFrame(ctx, 5);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return {};
        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_RESET ||
            hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_INVALID_CALL) {
            return {};
        }
        if (FAILED(hr)) {
            if (ctx.hasAcquiredFrame) m_dda.ReleaseFrame();
            return {};
        }
        if (!ctx.texture) {
            if (ctx.hasAcquiredFrame) m_dda.ReleaseFrame();
            return {};
        }

        int cw = std::min(m_regionW, std::max(1, m_screenW));
        int ch = std::min(m_regionH, std::max(1, m_screenH));
        int left = std::max(0, (m_screenW - cw) / 2);
        int top = std::max(0, (m_screenH - ch) / 2);

        D3D11_BOX box{ (UINT)left, (UINT)top, 0, (UINT)(left + cw), (UINT)(top + ch), 1 };
        m_dda.Context()->CopySubresourceRegion(m_staging, 0, 0, 0, 0, ctx.texture, 0, &box);
        m_dda.ReleaseFrame();
        ctx.texture->Release();

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(m_dda.Context()->Map(m_staging, 0, D3D11_MAP_READ, 0, &mapped)))
            return {};

        RawImage result(m_regionH, m_regionW, 4);
        for (int y = 0; y < m_regionH; y++)
            memcpy(result.data + y * m_regionW * 4,
                   (uint8_t*)mapped.pData + y * mapped.RowPitch, m_regionW * 4);
        m_dda.Context()->Unmap(m_staging, 0);
        return result;
    }

private:
    bool createStaging() {
        SafeRelease(&m_staging);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_regionW;
        desc.Height = m_regionH;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT hr = m_dda.Device()->CreateTexture2D(&desc, nullptr, &m_staging);
        if (FAILED(hr))
            fprintf(stderr, "[Capture] CreateTexture2D(staging) failed hr=0x%x\n", hr);
        return SUCCEEDED(hr);
    }
};

// ============================================================
// GPUJPEG Encoder Wrapper
// ============================================================
class JpegEncoder {
    gpujpeg_encoder* m_encoder = nullptr;
    bool m_initialized = false;

public:
    ~JpegEncoder() { shutdown(); }

    bool initialize(int device_id = 0) {
        if (m_initialized) return true;
        if (gpujpeg_init_device(device_id, 0) != 0) return false;
        m_encoder = gpujpeg_encoder_create(nullptr);
        if (!m_encoder) return false;
        m_initialized = true;
        return true;
    }

    void shutdown() {
        if (m_encoder) { gpujpeg_encoder_destroy(m_encoder); m_encoder = nullptr; }
        m_initialized = false;
    }

    bool isInitialized() const { return m_initialized; }

    std::vector<uint8_t> encode(const RawImage& bgra, int quality = 90) {
        if (!m_initialized || !m_encoder) {
            fprintf(stderr, "[JpegEncoder] not initialized\n");
            return {};
        }
        if (bgra.empty() || bgra.channels != 4) {
            fprintf(stderr, "[JpegEncoder] invalid input\n");
            return {};
        }

        // BGRA → RGB
        std::vector<uint8_t> rgb(static_cast<size_t>(bgra.width) * bgra.height * 3);
        bgra_to_rgb(bgra.data, rgb.data(), bgra.width, bgra.height);

        gpujpeg_parameters params;
        gpujpeg_set_default_parameters(&params);
        params.quality = quality;
        gpujpeg_parameters_chroma_subsampling(&params, GPUJPEG_SUBSAMPLING_444);

        gpujpeg_image_parameters img_params;
        gpujpeg_image_set_default_parameters(&img_params);
        img_params.width = bgra.width;
        img_params.height = bgra.height;
        img_params.color_space = GPUJPEG_RGB;
        img_params.pixel_format = GPUJPEG_444_U8_P012;

        gpujpeg_encoder_input input;
        gpujpeg_encoder_input_set_image(&input, rgb.data());

        uint8_t* compressed = nullptr;
        size_t compressed_size = 0;

        if (gpujpeg_encoder_encode(m_encoder, &params, &img_params, &input,
                                   &compressed, &compressed_size) != 0) {
            fprintf(stderr, "[JpegEncoder] encode failed\n");
            return {};
        }
        return { compressed, compressed + compressed_size };
    }
};

// ============================================================
// Main
// ============================================================
static std::atomic<bool> g_running{ true };

static void signalHandler(int) {
    g_running.store(false);
}

struct Stats {
    uint64_t frames_sent = 0;
    double total_cap_ms = 0.0;
    double total_encode_ms = 0.0;
    double total_send_ms = 0.0;
};

int main(int argc, char* argv[]) {
    std::string host = "192.168.137.2";
    int port = 12345;
    int monitor = 0;
    int crop_size = 640;
    int jpeg_quality = 80;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--monitor" && i + 1 < argc) monitor = std::atoi(argv[++i]);
        else if (arg == "--crop-size" && i + 1 < argc) crop_size = std::atoi(argv[++i]);
        else if (arg == "--jpeg-quality" && i + 1 < argc) jpeg_quality = std::atoi(argv[++i]);
    }

    printf("[frame_sender] host=%s port=%d monitor=%d crop_size=%d jpeg_quality=%d\n",
           host.c_str(), port, monitor, crop_size, jpeg_quality);

    timeBeginPeriod(1);
    std::signal(SIGINT, signalHandler);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[frame_sender] WSAStartup failed\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "[frame_sender] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr) != 1) {
        fprintf(stderr, "[frame_sender] invalid host: %s\n", host.c_str());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    JpegEncoder codec;
    if (!codec.initialize()) {
        fprintf(stderr, "[frame_sender] JpegEncoder::initialize() failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    FrameCapture capturer(crop_size, crop_size, monitor);
    if (!capturer.valid()) {
        fprintf(stderr, "[frame_sender] FrameCapture init failed\n");
        codec.shutdown();
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    Stats stats;
    uint64_t frame_seq = 0;
    uint16_t frame_id = 0;
    auto fps_start = std::chrono::high_resolution_clock::now();
    uint64_t fps_counter = 0;

    printf("[frame_sender] starting main loop (Ctrl+C to stop)...\n");

    constexpr auto frame_interval = std::chrono::milliseconds(10);
    auto next_frame_time = std::chrono::steady_clock::now();

    while (g_running.load()) {
        next_frame_time += frame_interval;
        auto t_cap_start = std::chrono::high_resolution_clock::now();
        RawImage frame = capturer.GetNextFrame();
        auto t_cap_end = std::chrono::high_resolution_clock::now();
        double cap_ms = std::chrono::duration<double, std::milli>(t_cap_end - t_cap_start).count();
        if (frame.empty()) continue;
        stats.total_cap_ms += cap_ms;

        // Skip crop when the captured frame already matches the target size
        // (DDA staging is sized to crop_size, so this is the common path).
        RawImage cropped;
        if (frame.width > crop_size && frame.height > crop_size)
            cropped = center_crop_bgra(frame, crop_size);
        else
            cropped = std::move(frame);

        int out_w = cropped.width;
        int out_h = cropped.height;

        // Encode to JPEG
        auto t_enc_start = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> jpeg_data = codec.encode(cropped, jpeg_quality);
        auto t_enc_end = std::chrono::high_resolution_clock::now();
        double enc_ms = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();
        stats.total_encode_ms += enc_ms;

        if (jpeg_data.empty()) {
            fprintf(stderr, "[frame_sender] JPEG encode failed for frame %llu\n",
                    (unsigned long long)frame_seq);
            ++frame_seq; ++frame_id;
            continue;
        }

        // Build frame header
        FrameHeader header;
        header.width = static_cast<uint32_t>(out_w);
        header.height = static_cast<uint32_t>(out_h);
        header.frame_seq = frame_seq;
        header.capture_timestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        header.format = FORMAT_JPEG;
        header.jpeg_size = static_cast<uint32_t>(jpeg_data.size());

        std::vector<uint8_t> frame_buf;
        encode_length_prefixed(header, jpeg_data.data(), jpeg_data.size(), frame_buf);

        std::vector<std::vector<uint8_t>> datagrams;
        FragmentEncoder::encode(frame_id, frame_buf.data(), frame_buf.size(), datagrams);

        auto t_send_start = std::chrono::high_resolution_clock::now();
        for (const auto& dg : datagrams) {
            sendto(sock, (const char*)dg.data(), (int)dg.size(), 0,
                   (const sockaddr*)&destAddr, (int)sizeof(destAddr));
        }
        auto t_send_end = std::chrono::high_resolution_clock::now();
        stats.total_send_ms += std::chrono::duration<double, std::milli>(t_send_end - t_send_start).count();

        ++stats.frames_sent;
        ++frame_seq;
        ++frame_id;
        ++fps_counter;

        if (fps_counter % 100 == 0) {
            double elapsed = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - fps_start).count();
            printf("[frame_sender] frame=%llu fps=%.1f cap=%.2fms enc=%.2fms\n",
                   (unsigned long long)stats.frames_sent, 100.0 / elapsed, cap_ms, enc_ms);
            fps_start = std::chrono::high_resolution_clock::now();
        }

        std::this_thread::sleep_until(next_frame_time);
    }

    printf("\n[frame_sender] shutting down...\n");
    printf("[frame_sender] total frames sent: %llu\n", (unsigned long long)stats.frames_sent);
    if (stats.frames_sent > 0) {
        printf("[frame_sender] avg capture: %.2f ms\n", stats.total_cap_ms / stats.frames_sent);
        printf("[frame_sender] avg encode:  %.2f ms\n", stats.total_encode_ms / stats.frames_sent);
        printf("[frame_sender] avg send:    %.2f ms\n", stats.total_send_ms / stats.frames_sent);
    }

    closesocket(sock);
    WSACleanup();
    timeEndPeriod(1);
    return 0;
}
