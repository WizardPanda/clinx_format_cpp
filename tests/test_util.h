#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "clxcpp/clx.hpp"

namespace test {

inline int& fail_count() {
  static int n = 0;
  return n;
}
inline int& pass_count() {
  static int n = 0;
  return n;
}

struct registry {
  static std::vector<std::pair<std::string, std::function<void()>>>& all() {
    static std::vector<std::pair<std::string, std::function<void()>>> v;
    return v;
  }
};

struct adder {
  adder(const char* name, std::function<void()> fn) {
    registry::all().emplace_back(name, std::move(fn));
  }
};

inline int run_all() {
  for (auto& [name, fn] : registry::all()) {
    std::fflush(stdout);
    int before = fail_count();
    std::printf("RUN  %s\n", name.c_str());
    std::fflush(stdout);
    try {
      fn();
    } catch (const std::exception& e) {
      ++fail_count();
      std::printf("  UNCAUGHT EXCEPTION: %s\n", e.what());
    } catch (...) {
      ++fail_count();
      std::printf("  UNCAUGHT UNKNOWN EXCEPTION\n");
    }
    if (fail_count() == before) {
      ++pass_count();
      std::printf("PASS %s\n", name.c_str());
    } else {
      std::printf("FAIL %s (%d failures)\n", name.c_str(), fail_count() - before);
    }
    std::fflush(stdout);
  }
  std::printf("\n%d passed, %d failed\n", pass_count(), fail_count());
  std::fflush(stdout);
  return fail_count() == 0 ? 0 : 1;
}

}  // namespace test

#define TEST(name)                                                    \
  static void test_##name();                                          \
  static test::adder test_adder_##name(#name, &test_##name);          \
  static void test_##name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ++test::fail_count();                                            \
      std::printf("  CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                  #cond);                                              \
    }                                                                  \
  } while (0)

#define CHECK_EQ(a, b)                                                  \
  do {                                                                  \
    auto va = (a);                                                      \
    auto vb = (b);                                                      \
    if (!(va == vb)) {                                                  \
      ++test::fail_count();                                             \
      std::printf("  CHECK_EQ failed at %s:%d: %s == %s\n", __FILE__,   \
                  __LINE__, #a, #b);                                    \
    }                                                                   \
  } while (0)

// --- shared helpers ---------------------------------------------------------

inline std::vector<uint8_t> read_file_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open: " + path);
  in.seekg(0, std::ios::end);
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<std::size_t>(size));
  if (size > 0) in.read(reinterpret_cast<char*>(data.data()), size);
  return data;
}

inline std::string read_file_text(const std::string& path) {
  auto bytes = read_file_bytes(path);
  return std::string(bytes.begin(), bytes.end());
}

// Minimal grayscale-PNG decoder returning (width, height, bits, pixels_le).
struct decoded_png {
  int width = 0;
  int height = 0;
  int bits = 0;
  std::vector<uint8_t> pixels_le;
};

inline uint32_t read_be32(const std::vector<uint8_t>& d, std::size_t off) {
  return (static_cast<uint32_t>(d[off]) << 24) |
         (static_cast<uint32_t>(d[off + 1]) << 16) |
         (static_cast<uint32_t>(d[off + 2]) << 8) |
         static_cast<uint32_t>(d[off + 3]);
}

extern "C" unsigned long mz_uncompress(unsigned char*, unsigned long*,
                                       const unsigned char*, unsigned long);

inline decoded_png decode_png(const std::vector<uint8_t>& data) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  decoded_png out;
  std::vector<uint8_t> idat;
  std::size_t pos = 8;
  int width = 0, height = 0, bits = 0, ctype = -1;
  while (pos + 8 <= data.size()) {
    uint32_t length = read_be32(data, pos);
    pos += 4;
    std::string tag(data.begin() + static_cast<std::ptrdiff_t>(pos),
                    data.begin() + static_cast<std::ptrdiff_t>(pos + 4));
    pos += 4;
    std::vector<uint8_t> chunk(data.begin() + static_cast<std::ptrdiff_t>(pos),
                               data.begin() +
                                   static_cast<std::ptrdiff_t>(pos + length));
    pos += length + 4;  // skip data + crc
    if (tag == "IHDR") {
      width = static_cast<int>(read_be32(chunk, 0));
      height = static_cast<int>(read_be32(chunk, 4));
      bits = chunk[8];
      ctype = chunk[9];
    } else if (tag == "IDAT") {
      idat.insert(idat.end(), chunk.begin(), chunk.end());
    } else if (tag == "IEND") {
      break;
    }
  }
  CHECK(ctype == 0);
  std::vector<uint8_t> raw;
  raw.resize((static_cast<std::size_t>(width) * bits / 8 + 1) *
             static_cast<std::size_t>(height));
  unsigned long dest_len = static_cast<unsigned long>(raw.size());
  int rc = mz_uncompress(raw.data(), &dest_len, idat.data(),
                         static_cast<unsigned long>(idat.size()));
  CHECK(rc == 0);
  raw.resize(dest_len);
  std::size_t row_len = static_cast<std::size_t>(width * bits / 8);
  CHECK(raw.size() == (row_len + 1) * static_cast<std::size_t>(height));
  std::vector<uint8_t> pixels;
  for (int y = 0; y < height; ++y) {
    std::size_t base = static_cast<std::size_t>(y) * (row_len + 1);
    CHECK(raw[base] == 0);
    pixels.insert(pixels.end(), raw.begin() + static_cast<std::ptrdiff_t>(base + 1),
                  raw.begin() + static_cast<std::ptrdiff_t>(base + 1 + row_len));
  }
  if (bits == 16) {
    for (std::size_t i = 0; i + 1 < pixels.size(); i += 2) {
      std::swap(pixels[i], pixels[i + 1]);
    }
  }
  out.width = width;
  out.height = height;
  out.bits = bits;
  out.pixels_le = std::move(pixels);
  return out;
}

// Extract the pixel strip of a vendor/instrument TIFF (starts at offset 24).
inline std::vector<uint8_t> tiff_strip(const std::vector<uint8_t>& tif,
                                       std::size_t byte_count) {
  CHECK(tif.size() >= 24 + byte_count);
  return std::vector<uint8_t>(tif.begin() + static_cast<std::ptrdiff_t>(24),
                              tif.begin() +
                                  static_cast<std::ptrdiff_t>(24 + byte_count));
}
