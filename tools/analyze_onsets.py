#!/usr/bin/env python3
"""Find click/hit onsets in a WAV file (PCM16/24/32 or float32) and print the
intervals between them and each onset's peak level. Standard library only.

    python3 tools/analyze_onsets.py capture.wav [--threshold 0.02] [--min-gap 0.1]
"""
import argparse, math, struct


def read_wav(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    i, fmt, pcm = 12, None, None
    while i < len(data) - 8:
        cid, size = data[i:i + 4], struct.unpack("<I", data[i + 4:i + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", data[i + 8:i + 24])
        elif cid == b"data":
            pcm = data[i + 8:i + 8 + size]
            break
        i += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    width = bits // 8
    count = len(pcm) // width
    if tag == 3:
        samples = struct.unpack(f"<{count}f", pcm[:count * 4])
    elif bits == 16:
        samples = [v / 32768 for v in struct.unpack(f"<{count}h", pcm[:count * 2])]
    elif bits == 24:
        samples = [int.from_bytes(pcm[k:k + 3], "little", signed=True) / 8388608 for k in range(0, count * 3, 3)]
    elif bits == 32:
        samples = [v / 2147483648 for v in struct.unpack(f"<{count}i", pcm[:count * 4])]
    else:
        raise SystemExit(f"unsupported format tag={tag} bits={bits}")
    return rate, channels, samples[0::channels]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--threshold", type=float, default=0.02, help="onset level (linear)")
    ap.add_argument("--min-gap", type=float, default=0.1, help="silence needed before an onset (s)")
    args = ap.parse_args()

    rate, channels, x = read_wav(args.wav)
    onsets, quiet = [], 10**12
    for k, v in enumerate(x):
        if abs(v) > args.threshold:
            if quiet > rate * args.min_gap:
                onsets.append(k)
            quiet = 0
        else:
            quiet += 1

    print(f"{args.wav}: {len(x) / rate:.2f} s @ {rate} Hz, {channels} ch; {len(onsets)} onsets")
    if len(onsets) > 1:
        gaps = [(b - a) / rate for a, b in zip(onsets, onsets[1:])]
        print("intervals (s):", " ".join(f"{g:.4f}" for g in gaps))
    if onsets:
        peaks = [20 * math.log10(max(abs(v) for v in x[o:o + int(rate * 0.03)])) for o in onsets]
        print("peaks (dBFS):", " ".join(f"{p:.1f}" for p in peaks))


if __name__ == "__main__":
    main()
