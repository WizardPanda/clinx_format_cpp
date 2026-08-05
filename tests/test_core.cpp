#include "test_util.h"

#include <string>

using namespace clxcpp;

static std::string data_dir() {
  return CLXCPP_TEST_DATA_DIR;
}

TEST(core_magic_and_version) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    CHECK_EQ(f.magic, kMagic);
    CHECK_EQ(f.software, std::string("Clx695"));
    CHECK_EQ(f.format_version, (int64_t)3);
  }
}

TEST(core_build_date) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    CHECK(f.build_datetime.has_value());
    CHECK_EQ(f.build_datetime->year, 2023);
    CHECK_EQ(f.build_datetime->month, 12);
    CHECK_EQ(f.build_datetime->day, 28);
    CHECK_EQ(f.build_datetime->hour, 11);
    CHECK_EQ(f.build_datetime->minute, 13);
  }
}

TEST(core_sample_name_and_exposure) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  CHECK_EQ(f.sample_name, std::string("Samp1_20260804_161544"));
  CHECK_EQ(f.exposure_ms, (int64_t)6946);
  clx_file g = load(data_dir() + "/Samp2_20260717_194348_00.00.332.clx");
  CHECK_EQ(g.sample_name, std::string("Samp2_20260717_194348"));
  CHECK_EQ(g.exposure_ms, (int64_t)332);
}

TEST(core_capture_time) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  CHECK(f.capture_time.has_value());
  CHECK_EQ(f.capture_time->year, 2026);
  CHECK_EQ(f.capture_time->month, 8);
  CHECK_EQ(f.capture_time->day, 4);
  CHECK_EQ(f.capture_time->hour, 16);
  CHECK_EQ(f.capture_time->minute, 15);
  CHECK_EQ(f.capture_time->second, 57);
  CHECK_EQ(f.capture_time->microsecond, 224000);
}

TEST(core_image_count_and_dimensions) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)687);
  CHECK_EQ(f.images[0].height(), (int64_t)550);
  CHECK_EQ(f.images[1].width(), (int64_t)687);
  CHECK_EQ(f.images[1].height(), (int64_t)550);
  for (auto& img : f.images) {
    CHECK_EQ(img.bits_per_sample(), (int64_t)16);
    CHECK_EQ(img.byte_count(), img.width() * img.height() * 2);
  }
}

TEST(core_descriptor_min_max) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  CHECK_EQ(f.images[0].min_value(), (int64_t)0);
  CHECK_EQ(f.images[0].max_value(), (int64_t)65535);
  CHECK_EQ(f.images[1].min_value(), (int64_t)1200);
  CHECK_EQ(f.images[1].max_value(), (int64_t)65535);
}

TEST(core_descriptor_offsets) {
  clx_file f = load(data_dir() + "/Samp1_20260804_161544_00.06.946.clx");
  CHECK_EQ(f.images[0].descriptor.offset, (int64_t)814);
  CHECK_EQ(f.images[1].descriptor.offset, (int64_t)757070);
  CHECK_EQ(f.images[0].pixel_offset(), (int64_t)848);
}

TEST(core_pixels_identical_to_exported_tiff_strips) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    for (std::size_t i = 0; i < f.images.size(); ++i) {
      std::vector<uint8_t> tif =
          read_file_bytes(data_dir() + "/" + key + "_" + std::to_string(i) +
                          "_16bit.tif");
      auto strip = tiff_strip(tif, static_cast<std::size_t>(f.images[i].byte_count()));
      CHECK_EQ(strip, f.images[i].pixel_buf);
    }
  }
}

TEST(core_channel_labels) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    auto labels = f.channel_labels();
    CHECK_EQ(labels.size(), (std::size_t)2);
    CHECK_EQ(labels[0], std::string("brightfield"));
    CHECK_EQ(labels[1], std::string("fluorescence"));
  }
}

TEST(core_trailer_size) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    CHECK_EQ(f.trailer.size(), (std::size_t)6456);
    CHECK(f.raw_trailer_info_.exposure_ms_matches_header);
    CHECK(f.raw_trailer_info_.has_gray_pal);
    CHECK_EQ(f.raw_trailer_info_.gray_pal, std::string("Gray.pal"));
    CHECK(f.raw_trailer_info_.has_build_date);
    CHECK_EQ(f.raw_trailer_info_.build_date_text, std::string(kBuildDateString));
  }
}

TEST(helpers_ole_to_datetime) {
  datetime d = ole_to_datetime(46238.0);
  CHECK_EQ(d.year, 2026);
  CHECK_EQ(d.month, 8);
  CHECK_EQ(d.day, 4);
  datetime z = ole_to_datetime(0.0);
  CHECK_EQ(z.year, 1899);
  CHECK_EQ(z.month, 12);
  CHECK_EQ(z.day, 30);
}

TEST(helpers_parse_build_date) {
  auto d = parse_build_date("202312281113");
  CHECK(d.has_value());
  CHECK_EQ(d->year, 2023);
  CHECK_EQ(d->month, 12);
  CHECK_EQ(d->day, 28);
  CHECK_EQ(d->hour, 11);
  CHECK_EQ(d->minute, 13);
  auto d2 = parse_build_date("20231228");
  CHECK(d2.has_value());
  CHECK_EQ(d2->day, 28);
  CHECK(!parse_build_date("garbage").has_value());
  CHECK(!parse_build_date("").has_value());
}

TEST(helpers_parse_filename) {
  auto info = parse_filename("Samp1_20260804_161544_00.06.946.clx");
  CHECK(info.has_value());
  CHECK_EQ(info->sample, std::string("Samp1"));
  CHECK_EQ(info->date, std::string("20260804"));
  CHECK_EQ(info->time, std::string("161544"));
  CHECK_EQ(info->exposure_ms, (int64_t)6946);
  CHECK(info->capture_time.has_value());
  CHECK_EQ(info->capture_time->hour, 16);
  CHECK_EQ(info->capture_time->minute, 15);
  CHECK_EQ(info->capture_time->second, 44);

  auto info2 = parse_filename("Samp2_20260717_194348_00.00.332.clx");
  CHECK(info2.has_value());
  CHECK_EQ(info2->exposure_ms, (int64_t)332);
  CHECK(!parse_filename("random.bin").has_value());
}

TEST(errors_bad_magic) {
  std::vector<uint8_t> bad(1000, 0);
  bool threw = false;
  try {
    parse(bad, "x.clx");
  } catch (const format_error&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(errors_tiff_detected) {
  auto tif = read_file_bytes(data_dir() +
                             "/Samp1_20260804_161544_00.06.946_0_16bit.tif");
  bool threw = false;
  std::string msg;
  try {
    parse(tif, "x.clx");
  } catch (const format_error& e) {
    threw = true;
    msg = e.what();
  }
  CHECK(threw);
  CHECK(msg.find("TIFF") != std::string::npos);
}
