#include "clxcpp/clx.hpp"

#include <fstream>

namespace clxcpp {

namespace {

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

}  // namespace

std::vector<uint8_t> encode_tiff(int64_t width, int64_t height,
                                 int64_t bits_per_sample,
                                 const std::vector<uint8_t>& pixels, int dpi,
                                 int photometric) {
  if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 32) {
    throw std::invalid_argument("unsupported bits per sample");
  }
  int64_t byte_count = width * height * bits_per_sample / 8;
  if (static_cast<int64_t>(pixels.size()) != byte_count) {
    throw std::invalid_argument("pixel buffer size does not match dimensions");
  }

  // TIFF field types
  const int kShort = 3;
  const int kLong = 4;
  const int kRational = 5;

  int64_t ifd_size = 2 + 13 * 12 + 4;
  int64_t ifd_offset = 8 + byte_count;
  int64_t xres_offset = ifd_offset + ifd_size;
  int64_t yres_offset = xres_offset + 8;

  std::vector<uint8_t> out;
  out.reserve(static_cast<std::size_t>(yres_offset + 16));

  // header
  out.push_back('I');
  out.push_back('I');
  out.push_back('*');
  out.push_back(0);
  put_u32(out, static_cast<uint32_t>(ifd_offset));

  // pixel strip
  out.insert(out.end(), pixels.begin(), pixels.end());

  // IFD with 13 tags + next-IFD pointer
  put_u16(out, 13);
  auto entry = [&](uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
    put_u16(out, tag);
    put_u16(out, type);
    put_u32(out, count);
    put_u32(out, value);
  };
  entry(254, kLong, 1, 0);                // NewSubfileType
  entry(256, kLong, 1, static_cast<uint32_t>(width));   // ImageWidth
  entry(257, kLong, 1, static_cast<uint32_t>(height));  // ImageLength
  entry(258, kShort, 1, static_cast<uint32_t>(bits_per_sample));
  entry(259, kShort, 1, 1);               // Compression = none
  entry(262, kShort, 1, static_cast<uint32_t>(photometric));
  entry(273, kLong, 1, 8);                // StripOffsets
  entry(277, kShort, 1, 1);               // SamplesPerPixel
  entry(278, kLong, 1, static_cast<uint32_t>(height));   // RowsPerStrip
  entry(279, kLong, 1, static_cast<uint32_t>(byte_count));  // StripByteCounts
  entry(282, kRational, 1, static_cast<uint32_t>(xres_offset));
  entry(283, kRational, 1, static_cast<uint32_t>(yres_offset));
  entry(296, kShort, 1, 2);               // ResolutionUnit = inch
  put_u32(out, 0);                        // next IFD

  // rationals: dpi / 1
  put_u32(out, static_cast<uint32_t>(dpi));
  put_u32(out, 1);
  put_u32(out, static_cast<uint32_t>(dpi));
  put_u32(out, 1);

  return out;
}

std::vector<uint8_t> clx_image::to_tiff_bytes(int dpi) const {
  return encode_tiff(width(), height(), bits_per_sample(), pixel_buf, dpi, 1);
}

void clx_image::save_tiff(const std::string& path, int dpi) const {
  auto bytes = to_tiff_bytes(dpi);
  std::ofstream out(path, std::ios::binary);
  if (!out) throw format_error("cannot open file for writing: " + path);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

}  // namespace clxcpp
