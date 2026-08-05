# Clinx `.clx` File Format Specification

> **中文文档：[《`.clx` 文件格式规范（中文）》](clx-format-spec.zh-CN.md)**

This document describes the binary format of the `.clx` files produced by Clinx
chemiluminescence imaging instruments, as reverse-engineered and validated by
the `clxparser` project.

> **Portability warning.** The format description below is derived from a small
> number of captures produced by a single device (software `Clx695`, format
> version 3, build 2023-12-28). It is **not guaranteed** that all `.clx` files
> from all Clinx products follow this exact layout. See
> [Known unknowns](#known-unknowns-and-risks).

---

## 1. Overview

A `.clx` capture is a single binary file that bundles:

- acquisition **metadata** (sample name, capture time, exposure, software
  version, build date),
- the raw **16-bit pixel data** of the bright-field and fluorescence images,
- a trailing **settings/statistics** block.

The images are stored as raw, uncompressed, little-endian `uint16` samples in
row-major order — the same pixel strip the instrument's own software writes
into the exported `.tif` files.

**Byte order:** all multi-byte integers and floats are little-endian (Intel).

**File layout:**

```
 0x0000  ┌──────────────────────────────────────────────┐
         │ File header   (0x0000 – 0x0123)              │
 0x0124  ├──────────────────────────────────────────────┤
         │ Version block (0x0124 – 0x032D)              │
 0x032E  ├──────────────────────────────────────────────┤
         │ Image descriptor #1 (34 bytes)               │
 0x0350  ├──────────────────────────────────────────────┤
         │ Pixel data #1   (width·height·2 bytes)       │
         ├──────────────────────────────────────────────┤
         │ Version block #2                             │
         │ Image descriptor #2 (34 bytes)               │
         │ Pixel data #2                                │
         ├──────────────────────────────────────────────┤
         │ Trailer (6456 bytes)                         │
         └──────────────────────────────────────────────┘
```

The layout is a repeating sequence: each embedded image is preceded by a
`Version block + padding + Image descriptor`, and the first version block is
preceded by the file header. Every observed capture contains **two** images.

---

## 2. File header

The header occupies `0x0000`–`0x0123`.

| Offset | Size | Type | Name | Meaning |
|---|---|---|---|---|
| `0x000` | 4 | `u32` | `magic` | Format signature, always `0x000025EB` |
| `0x004` | 4 | `u32` | `field_1` | Constant `6` (semantics unknown) |
| `0x008` | 4 | `u32` | `field_2` | Constant `6` (semantics unknown) |
| `0x00C` | 8 | `f64` | `capture_time` | OLE Automation Date (see below) |
| `0x014` | 4 | `u32` | `exposure_ms` | Exposure duration in milliseconds |
| `0x018` | 22 | `char[22]` | `sample_name` | Null-padded ASCII sample name |
| `0x02E` | 246 | — | *(opaque)* | Leftover C++ heap pointers; not metadata |

### 2.1 Capture time (OLE Automation Date)

The `capture_time` field is a double holding an **OLE Automation Date**: the
number of days (with fraction) since `1899-12-30`.

| Sample | Raw value | Decoded |
|---|---|---|
| `Samp1` | `46238.67774564815` | `2026-08-04 16:15:57.224` |
| `Samp2` | `46220.82224699074` | `2026-07-17 19:44:02.140` |

The instrument's own filename timestamp (see §6) is a few seconds earlier than
this value; treat the header value as the authoritative capture time.

### 2.2 Sample name

`sample_name` is a fixed 22-byte ASCII field, null-padded when shorter. In the
observed files it holds the full capture stem, e.g. `Samp1_20260804_161544`
(22 characters, exactly filling the field).

### 2.3 Opaque header region

Bytes `0x02E`–`0x123` contain small structural constants interleaved with what
appear to be un-relocated memory addresses (heap pointers) left over from a C++
serialization. They carry no stable, interpretable meaning and are exposed
verbatim as `ClxFile.raw_header`.

---

## 3. Version block

The version block is repeated before **every** image (the first occurrence sits
at `0x0124`). Its size is fixed; the first image's descriptor always begins at
`0x032E` in the observed files.

| Offset (relative) | Size | Type | Name | Meaning |
|---|---|---|---|---|
| `0x000` | 4 | `u32` | `format_version` | Format version, `3` |
| `0x004` | 256 | `char[0x100]` | `software_id` | Software id, `"Clx695"` |
| `0x104` | 256 | `char[0x100]` | `build_date` | Build date `"202312281113"` (`YYYYMMDDHHMM`) |
| `0x204` | 6 | — | *(padding)* | Zero bytes |

`build_date` encodes the build time of the instrument software: `202312281113`
== `2023-12-28 11:13`.

---

## 4. Image descriptor

Each image is described by a 34-byte descriptor, immediately followed by its
raw pixel data.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| `0x00` | 2 | `u16` | `marker` | `0xC03E` — identifies a descriptor |
| `0x02` | 4 | `u32` | `type` | Image type constant, `2` or `4` |
| `0x06` | 4 | `u32` | `width` | Image width in pixels |
| `0x0A` | 4 | `u32` | `height` | Image height in pixels |
| `0x0E` | 4 | `u32` | `bits_per_sample` | Bit depth, `16` |
| `0x12` | 4 | `u32` | `max_value` | Reported pixel maximum |
| `0x16` | 4 | `u32` | `min_value` | Reported pixel minimum |
| `0x1A` | 4 | `u32` | `byte_count` | `width · height · bits / 8` |
| `0x1E` | 4 | `u32` | `reserved` | `0` |

- `max_value` / `min_value` match the actual pixel statistics of the image data
  in every observed file (e.g. a dim fluorescence channel reports its true peak,
  a saturated channel reports `65535`).
- `byte_count` always equals `width · height · bits_per_sample / 8`, which is
  the invariant the parser uses to reject false descriptors.

### 4.1 Image `type` field

The `type` field is **constant within a capture** and identical for both images.
Observed values:

| File | `type` |
|---|---|
| `Samp1` (687×550, binned) | `4` |
| `Samp2` (1375×1100, full resolution) | `2` |

Its semantics are not known; it may encode an acquisition mode, pixel storage
variant, or camera configuration.

### 4.2 Descriptor discovery

Because the descriptor position is tied to a fixed header layout that may differ
between software versions, `clxparser` does **not** rely on hard-coded offsets.
Instead it scans the file for the `0xC03E` marker and keeps only candidates
that satisfy all of:

1. `1 ≤ width ≤ 8192` and `1 ≤ height ≤ 8192`;
2. `bits_per_sample ∈ {8, 16, 32}`;
3. `byte_count == width · height · bits_per_sample / 8`;
4. `0 ≤ min_value ≤ max_value ≤ 2^bits − 1`;
5. the descriptor plus its pixel data fits inside the file.

This combination is extremely selective — in the two real captures the scan
finds exactly two descriptors and zero false positives inside the raw pixel
data.

---

## 5. Pixel data

Raw, uncompressed samples immediately follow the descriptor:

- **Layout:** row-major; row 0 first, pixels left to right within a row.
- **Sample format:** little-endian `uint16` (bit depth 16).
- **Size:** `width · height · 2` bytes = `byte_count`.

These bytes are **identical** to the pixel strip in the instrument-exported
TIFFs (verified for all four exported images in the test suite).

### 5.1 Channels

Every observed capture contains two images in a stable order:

| Index | Channel | Characteristics |
|---|---|---|
| `0` | Bright field | Higher mean / variance (evenly illuminated) |
| `1` | Fluorescence / chemiluminescence | Darker background with signal |

`clxparser` does not hard-code this; `ClxFile.channel_labels()` assigns the
labels by comparing mean intensities when exactly two images are present.

---

## 6. Filename convention

Instrument filenames embed the same metadata as the header:

```
{sample}_{YYYYMMDD}_{HHMMSS}_{MM.SS.mmm}.clx
```

| Part | Meaning | Example |
|---|---|---|
| `sample` | Sample name | `Samp1` |
| `YYYYMMDD` | Capture date | `20260804` |
| `HHMMSS` | Capture time | `161544` |
| `MM.SS.mmm` | Exposure (minutes.seconds.milliseconds) | `00.06.946` → 6946 ms |

The exposure encoded in the filename matches the header `exposure_ms` field in
every observed file.

---

## 7. Trailer

Everything after the last image's pixel data is the trailer — a fixed
**6456-byte** block in the observed captures. Its start is a small statistics
header:

| Offset | Size | Type | Meaning |
|---|---|---|---|
| `0x000` | 4 | `u32` | `10` (constant) |
| `0x004` | 4 | `u32` | `65535` (full scale) |
| `0x008` | 4 | `u32` | image `type` |
| `0x00C` | 4 | `u32` | image `type` |
| `0x010` | 4 | `u32` | exposure (ms) — matches the header |
| `0x014` | 4 | `u32` | image maximum |
| `0x018` | 4 | `u32` | image `type` |
| `0x01C` | 4 | `u32` | image `type` |

Embedded strings:

| Offset | String | Meaning |
|---|---|---|
| `0x108` | `"202312281113"` | Build date (same as version block) |
| `0x14C` | `"Gray.pal"` | Grayscale LUT / palette name |

The remainder of the trailer contains serialized display/analysis settings
(e.g. threshold values) interleaved with leftover heap pointers. It is exposed
verbatim as `ClxFile.trailer` and partially decoded into
`ClxFile.raw_trailer_info`.

---

## 8. Reference captures

The test suite ships two anonymized captures in `tests/data/`:

| Property | `Samp1_…_00.06.946.clx` | `Samp2_…_00.00.332.clx` |
|---|---|---|
| Resolution | 687 × 550 | 1375 × 1100 |
| Bit depth | 16 | 16 |
| Image `type` | 4 | 2 |
| Exposure | 6946 ms | 332 ms |
| Capture | 2026-08-04 16:15:57 | 2026-07-17 19:44:02 |
| Channel 0 min/max | 0 / 65535 | 0 / 65535 |
| Channel 1 min/max | 1200 / 65535 | 0 / 26182 |

Both were captured with the same device (software `Clx695`, format version 3,
build 2023-12-28), at different binning/exposure settings.

---

## 9. Known unknowns and risks

The following are documented but not fully understood:

- **`type` field** (2 vs 4) — constant per capture; meaning unknown.
- **Header `field_1` / `field_2`** (both `6`) — unknown.
- **Opaque header/trailer regions** — contain un-relocated heap pointers.
- **Descriptor `reserved`** field — always `0`.
- **Trailer length** — 6456 bytes in both samples; may vary with settings.
- **Compatibility** — the layout was derived from one device / software version;
  other Clinx products or software builds may write different fields, offsets,
  more images, or compressed pixels. Validate the parser against your own
  instrument output before relying on it.
