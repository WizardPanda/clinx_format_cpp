#include "test_util.h"

using namespace clxcpp;

static std::string data_dir() { return CLXCPP_TEST_DATA_DIR; }

TEST(png_signature_and_chunk_crc) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < 12; ++i) pixels.push_back(static_cast<uint8_t>(i));
  auto png = encode_png(4, 3, 8, pixels);
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  CHECK(png.size() >= 8);
  CHECK(std::equal(png.begin(), png.begin() + 8, sig));
  auto d = decode_png(png);  // validates CRC + structure
  CHECK_EQ(d.width, 4);
  CHECK_EQ(d.height, 3);
  CHECK_EQ(d.bits, 8);
}

TEST(png_8bit_roundtrip) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < 32; ++i) pixels.push_back(static_cast<uint8_t>(i));
  auto png = encode_png(4, 8, 8, pixels);
  auto d = decode_png(png);
  CHECK_EQ(d.width, 4);
  CHECK_EQ(d.height, 8);
  CHECK_EQ(d.bits, 8);
  CHECK_EQ(d.pixels_le, pixels);
}

TEST(png_16bit_roundtrip) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < 128; ++i) pixels.push_back(static_cast<uint8_t>(i));
  auto png = encode_png(8, 8, 16, pixels);
  auto d = decode_png(png);
  CHECK_EQ(d.width, 8);
  CHECK_EQ(d.height, 8);
  CHECK_EQ(d.bits, 16);
  CHECK_EQ(d.pixels_le, pixels);
}

TEST(png_size_mismatch_raises) {
  std::vector<uint8_t> pixels(10, 0);
  bool threw = false;
  try {
    encode_png(4, 4, 16, pixels);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(png_16bit_image_roundtrip) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    for (auto& img : f.images) {
      auto png = img.to_png_bytes();
      auto d = decode_png(png);
      CHECK_EQ(d.width, static_cast<int>(img.width()));
      CHECK_EQ(d.height, static_cast<int>(img.height()));
      CHECK_EQ(d.bits, 16);
      CHECK_EQ(d.pixels_le, img.pixel_buf);
    }
  }
}

TEST(png_preview_is_8bit) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  auto png = f.images[0].preview_png_bytes();
  auto d = decode_png(png);
  CHECK_EQ(d.bits, 8);
  CHECK_EQ(d.width, static_cast<int>(f.images[0].width()));
  CHECK_EQ(d.height, static_cast<int>(f.images[0].height()));
}

TEST(png_preview_known_low_high) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  // image 0: python percentile low=540 high=11798, min=0 max=65535
  auto png = f.images[0].preview_png_bytes((int64_t)540, (int64_t)11798);
  auto d = decode_png(png);

  // compute the raw pixel values in row-major order
  std::vector<uint16_t> vals;
  for (std::size_t i = 0; i + 1 < f.images[0].pixel_buf.size(); i += 2) {
    vals.push_back(static_cast<uint16_t>(f.images[0].pixel_buf[i] |
                                         (f.images[0].pixel_buf[i + 1] << 8)));
  }
  CHECK_EQ(vals.size(), d.pixels_le.size());
  CHECK_EQ(d.bits, 8);

  // check scaling: value 0 -> 0, value 65535 -> 255, and the low value -> 0
  for (std::size_t i = 0; i < vals.size(); ++i) {
    uint8_t expect = 0;
    int64_t v = vals[i];
    if (v <= 540) {
      expect = 0;
    } else if (v >= 11798) {
      expect = 255;
    } else {
      double scaled = (v - 540) * (255.0 / (11798 - 540));
      expect = static_cast<uint8_t>(scaled);
    }
    CHECK_EQ(static_cast<int>(d.pixels_le[i]), static_cast<int>(expect));
  }
}
