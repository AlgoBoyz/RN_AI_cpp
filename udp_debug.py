#!/usr/bin/env python3
"""UDP frame debug tool - dump raw fragment headers"""
import socket
import sys

PORT = 12345

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT))
    sock.settimeout(1.0)
    print(f"[debug] Listening on UDP port {PORT} (Ctrl+C to stop)")
    print(f"[debug] Format: frame_id:frag_idx/frag_count  size  magic_ok  ver_ok  hdr_ok")
    print("-" * 80)

    frame_count = {}
    try:
        while True:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue

            size = len(data)
            if size < 10:
                print(f"[debug] too small: {size} bytes from {addr}")
                continue

            # Parse fragment header
            magic = data[0:4]
            magic_ok = magic == b"SWTF"
            ver_ok = data[4] == 1
            hdr_len = data[5]
            hdr_ok = hdr_len == 10
            frame_id = int.from_bytes(data[6:8], "little")
            frag_cnt = data[8]
            frag_idx = data[9]

            # Track per-frame
            if frame_id not in frame_count:
                prev = frame_count.get(frame_id - 1, 0)
                frame_count[frame_id] = 0
            frame_count[frame_id] += 1

            flag = ""
            if frag_idx == 0:
                flag = " <-- FIRST"
            elif frag_idx == frag_cnt - 1:
                flag = " <-- LAST"

            print(f"  {frame_id:5d}:{frag_idx:2d}/{frag_cnt}  "
                  f"{size:4d}B  "
                  f"{'OK' if magic_ok else 'BAD'} "
                  f"{'OK' if ver_ok else 'BAD'} "
                  f"{'OK' if hdr_ok else 'BAD'}"
                  f"{flag}")

    except KeyboardInterrupt:
        print("\n" + "-" * 80)
        print("Per-frame fragment counts:")
        for fid in sorted(frame_count.keys()):
            exp = 0
            # Find expected count from any fragment of this frame
            print(f"  frame {fid}: {frame_count[fid]} fragments")
        print(f"Total frames: {len(frame_count)}")

if __name__ == "__main__":
    main()
