#include "clxcpp/clx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

extern "C" {
#include "miniz.h"
}

namespace clxcpp {

namespace {

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

uint32_t crc32_bytes(const uint8_t* data, std::size_t len) {
  return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data, len));
}

void put_be16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

// PNG chunk: length + tag + data + crc32(tag+data), all big-endian.
void append_chunk(std::vector<uint8_t>& out, const char* tag,
                  const std::vector<uint8_t>& data) {
  put_be32(out, static_cast<uint32_t>(data.size()));
  std::size_t tag_pos = out.size();
  out.insert(out.end(), tag, tag + 4);
  out.insert(out.end(), data.begin(), data.end());
  uint32_t crc = crc32_bytes(out.data() + tag_pos,
                             out.size() - tag_pos);
  put_be32(out, crc);
}

// tEXt chunk: "key\0text" pairs joined by '\n', latin-1 encoded. Mirrors the
// Python _tEXt_chunk (skips None/empty-limit/control-char edge cases).
std::vector<uint8_t> texl_chunk(
    const std::map<std::string, std::string>& metadata) {
  if (metadata.empty()) return {};
  std::vector<uint8_t> body;
  bool first = true;
  for (const auto& kv : metadata) {
    if (kv.second.empty()) continue;  // Python skips None values
    if (kv.first.find('\x00') != std::string::npos) continue;
    std::string text = kv.second;
    if (text.size() > 2000) text = text.substr(0, 2000);
    if (!first) body.push_back('\n');
    first = false;
    body.insert(body.end(), kv.first.begin(), kv.first.end());
    body.push_back('\x00');
    for (char c : text) body.push_back(static_cast<uint8_t>(c));
  }
  if (body.empty()) return {};
  std::vector<uint8_t> out;
  append_chunk(out, "tEXt", body);
  return out;
}

std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& raw,
                                   int level) {
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<uint8_t> out(bound);
  mz_ulong out_len = bound;
  int rc = mz_compress2(out.data(), &out_len, raw.data(),
                        static_cast<mz_ulong>(raw.size()), level);
  if (rc != MZ_OK) {
    throw std::runtime_error("deflate failed");
  }
  out.resize(out_len);
  return out;
}

}  // namespace

std::vector<uint8_t> encode_png(
    int64_t width, int64_t height, int bits,
    const std::vector<uint8_t>& pixel_bytes_le,
    const std::map<std::string, std::string>& metadata) {
  if (bits != 8 && bits != 16) {
    throw std::invalid_argument("unsupported PNG bit depth");
  }
  int64_t expected = width * height * bits / 8;
  if (static_cast<int64_t>(pixel_bytes_le.size()) != expected) {
    throw std::invalid_argument("pixel buffer size does not match dimensions");
  }

  // Convert little-endian samples to big-endian for PNG storage.
  std::vector<uint8_t> samples;
  if (bits == 16) {
    samples.resize(pixel_bytes_le.size());
    for (std::size_t i = 0; i + 1 < pixel_bytes_le.size(); i += 2) {
      samples[i] = pixel_bytes_le[i + 1];
      samples[i + 1] = pixel_bytes_le[i];
    }
  } else {
    samples = pixel_bytes_le;
  }

  std::size_t row_len = static_cast<std::size_t>(width * bits / 8);
  std::vector<uint8_t> raw(expected + height);
  std::size_t out_pos = 0;
  std::size_t src_pos = 0;
  for (int64_t r = 0; r < height; ++r) {
    raw[out_pos++] = 0;  // filter: None
    std::memcpy(raw.data() + out_pos, samples.data() + src_pos, row_len);
    out_pos += row_len;
    src_pos += row_len;
  }

  std::vector<uint8_t> compressed = zlib_compress(raw, 6);

  std::vector<uint8_t> ihdr;
  ihdr.reserve(13);
  put_be32(ihdr, static_cast<uint32_t>(width));
  put_be32(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(static_cast<uint8_t>(bits));
  ihdr.push_back(0);  // color type: grayscale
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace

  std::vector<uint8_t> out;
  out.insert(out.end(), kSignature, kSignature + 8);
  append_chunk(out, "IHDR", ihdr);
  auto tex = texl_chunk(metadata);
  out.insert(out.end(), tex.begin(), tex.end());
  append_chunk(out, "IDAT", compressed);
  append_chunk(out, "IEND", {});
  return out;
}

std::vector<uint8_t> clx_image::to_png_bytes(
    const std::map<std::string, std::string>& metadata) const {
  return encode_png(width(), height(), static_cast<int>(bits_per_sample()),
                    pixel_buf, metadata);
}

void clx_image::save_png(const std::string& path,
                         const std::map<std::string, std::string>& metadata) const {
  auto bytes = to_png_bytes(metadata);
  std::ofstream out(path, std::ios::binary);
  if (!out) throw format_error("cannot open file for writing: " + path);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

namespace {

// Percentile via linear interpolation on a sorted copy, replicating
// numpy.percentile's default ('linear') method.
double percentile_linear(std::vector<uint32_t> sorted, double q) {
  std::sort(sorted.begin(), sorted.end());
  std::size_t n = sorted.size();
  if (n == 0) return 0.0;
  double idx = static_cast<double>(n - 1) * q / 100.0;
  std::size_t lo = static_cast<std::size_t>(std::floor(idx));
  std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
  double frac = idx - std::floor(idx);
  double a = static_cast<double>(sorted[lo]);
  double b = static_cast<double>(sorted[hi]);
  return a + (b - a) * frac;
}

}  // namespace

std::vector<uint8_t> clx_image::preview_png_bytes(
    std::optional<int64_t> low, std::optional<int64_t> high,
    const std::map<std::string, std::string>& metadata) const {
  std::size_t n = pixel_buf.size() / (bits_per_sample() / 8);

  std::vector<uint32_t> values;
  values.reserve(n);
  if (bits_per_sample() == 16) {
    for (std::size_t i = 0; i + 1 < pixel_buf.size(); i += 2) {
      values.push_back(static_cast<uint32_t>(pixel_buf[i]) |
                       (static_cast<uint32_t>(pixel_buf[i + 1]) << 8));
    }
  } else if (bits_per_sample() == 8) {
    for (uint8_t v : pixel_buf) values.push_back(v);
  } else {
    throw std::invalid_argument("unsupported bits per sample");
  }

  if (!low || !high) {
    double lo = percentile_linear(values, 1.0);
    double hi = percentile_linear(values, 99.0);
    low = static_cast<int64_t>(lo);
    high = static_cast<int64_t>(hi);
  }
  if (*high <= *low) high = *low + 1;

  // numpy: (arr.astype(float32) - low) * (255.0/(high-low)) clipped to [0,255]
  float scale = static_cast<float>(255.0 / static_cast<double>(*high - *low));
  std::vector<uint8_t> scaled;
  scaled.reserve(values.size());
  for (uint32_t v : values) {
    float x = (static_cast<float>(static_cast<int64_t>(v)) -
               static_cast<float>(*low)) *
              scale;
    x = std::max(0.0f, std::min(255.0f, x));
    scaled.push_back(static_cast<uint8_t>(x));
  }

  return encode_png(width(), height(), 8, scaled, metadata);
}

}  // namespace clxcpp
