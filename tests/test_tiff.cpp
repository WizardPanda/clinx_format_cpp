#include "test_util.h"

using namespace clxcpp;

static std::string data_dir() { return CLXCPP_TEST_DATA_DIR; }

TEST(tiff_pixels_match_official_exports) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    for (std::size_t i = 0; i < f.images.size(); ++i) {
      auto official = read_file_bytes(data_dir() + "/" + key + "_" +
                                      std::to_string(i) + "_16bit.tif");
      auto mine = f.images[i].to_tiff_bytes();
      auto official_strip =
          tiff_strip(official, static_cast<std::size_t>(f.images[i].byte_count()));
      std::vector<uint8_t> my_strip(
          mine.begin() + static_cast<std::ptrdiff_t>(8),
          mine.begin() + static_cast<std::ptrdiff_t>(8 + f.images[i].byte_count()));
      CHECK_EQ(my_strip, official_strip);
    }
  }
}

TEST(tiff_standard_single_strip_layout) {
  clx_file f = load(data_dir() + "/Samp2_20260717_194348_00.00.332.clx");
  auto data = f.images[0].to_tiff_bytes();
  int64_t byte_count = f.images[0].byte_count();
  // 8-byte header + strip + 13-tag IFD + two rationals
  CHECK_EQ(data.size(), static_cast<std::size_t>(8 + byte_count + 2 + 13 * 12 + 4 + 16));
  CHECK(data.size() >= 4);
  CHECK(data[0] == 'I' && data[1] == 'I' && data[2] == '*' && data[3] == 0);
  uint32_t ifd_offset =
      static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
      (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
  CHECK_EQ(ifd_offset, static_cast<uint32_t>(8 + byte_count));
  uint16_t count = static_cast<uint16_t>(data[ifd_offset]) |
                   (static_cast<uint16_t>(data[ifd_offset + 1]) << 8);
  CHECK_EQ(count, (uint16_t)13);
}

TEST(tiff_writer_roundtrip_structure) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < 12; ++i) {
    pixels.push_back(0x01);
    pixels.push_back(0x02);
  }
  auto data = encode_tiff(4, 3, 16, pixels);
  CHECK(data[0] == 'I' && data[1] == 'I' && data[2] == '*' && data[3] == 0);
  CHECK_EQ(data.size(), static_cast<std::size_t>(8 + 24 + 162 + 16));
  CHECK_EQ(std::vector<uint8_t>(data.begin() + 8, data.begin() + 32), pixels);
}

TEST(tiff_writer_size_mismatch_raises) {
  std::vector<uint8_t> pixels(100, 0);
  bool threw = false;
  try {
    encode_tiff(10, 10, 16, pixels);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(tiff_writer_bad_bits_raises) {
  std::vector<uint8_t> pixels(100, 0);
  bool threw = false;
  try {
    encode_tiff(10, 10, 7, pixels);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}
