#include "test_util.h"

using namespace clxcpp;

static std::string data_dir() {
  return CLXCPP_TEST_DATA_DIR;
}

namespace {

// Build a minimal, self-contained .clx in memory: a valid magic + version
// block, a sample name, and two 16-bit image descriptors (with the given marker
// word) followed by pixel data. Used to exercise descriptor discovery without
// relying on the golden capture files.
std::vector<uint8_t> make_synthetic_clx(uint16_t marker, int width, int height,
                                        const std::string& sample_name) {
  const std::size_t byte_count = static_cast<std::size_t>(width) * height * 2;
  const std::size_t desc0 = 0x400;
  const std::size_t desc1 = desc0 + kDescriptorSize + byte_count;
  std::vector<uint8_t> data(desc1 + kDescriptorSize + byte_count, 0);

  auto w16 = [&](std::size_t off, uint16_t v) {
    data[off] = static_cast<uint8_t>(v & 0xFF);
    data[off + 1] = static_cast<uint8_t>(v >> 8);
  };
  auto w32 = [&](std::size_t off, uint32_t v) {
    data[off] = static_cast<uint8_t>(v & 0xFF);
    data[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    data[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    data[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
  };

  w32(0, kMagic);
  for (std::size_t i = 0; i < sample_name.size() && i < kSampleNameMaxSize; ++i) {
    data[0x18 + i] = static_cast<uint8_t>(sample_name[i]);
  }
  w32(0x0124, 3);  // format_version
  const char* soft = "Clx695";
  for (std::size_t i = 0; soft[i]; ++i) {
    data[0x0128 + i] = static_cast<uint8_t>(soft[i]);
  }

  auto place = [&](std::size_t off) {
    w16(off, marker);
    w32(off + 2, 4);  // type
    w32(off + 6, static_cast<uint32_t>(width));
    w32(off + 10, static_cast<uint32_t>(height));
    w32(off + 14, 16);  // bits_per_sample
    w32(off + 18, 65535);  // max_value
    w32(off + 22, 0);  // min_value
    w32(off + 26, static_cast<uint32_t>(byte_count));
    w32(off + 30, 0);  // reserved
  };
  place(desc0);
  place(desc1);
  return data;
}

}  // namespace

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
                   "Samp2_20260717_194348_00.00.332",
                   "Samp3_20250721_183436_01.30.000"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    auto labels = f.channel_labels();
    CHECK_EQ(labels.size(), (std::size_t)2);
    CHECK_EQ(labels[0], std::string("brightfield"));
    CHECK_EQ(labels[1], std::string("fluorescence"));
  }
}

TEST(core_trailer_size) {
  for (auto key : {"Samp1_20260804_161544_00.06.946",
                   "Samp2_20260717_194348_00.00.332",
                   "Samp3_20250721_183436_01.30.000"}) {
    clx_file f = load(data_dir() + "/" + key + ".clx");
    CHECK_EQ(f.trailer.size(), (std::size_t)6456);
    CHECK(f.raw_trailer_info_.exposure_ms_matches_header);
    CHECK(f.raw_trailer_info_.has_gray_pal);
    CHECK_EQ(f.raw_trailer_info_.gray_pal, std::string("Gray.pal"));
    CHECK(f.raw_trailer_info_.has_build_date);
    CHECK_EQ(f.raw_trailer_info_.build_date_text, std::string(kBuildDateString));
  }
}

TEST(core_samp3_metadata) {
  // Samp3 is a long-exposure (90 s) capture; regression test ensuring the
  // stable channel order (0=brightfield, 1=fluorescence) still applies.
  clx_file f = load(data_dir() + "/Samp3_20250721_183436_01.30.000.clx");
  CHECK_EQ(f.magic, kMagic);
  CHECK_EQ(f.sample_name, std::string("Samp3_20250721_183436"));
  CHECK_EQ(f.exposure_ms, (int64_t)90000);
  CHECK_EQ(f.software, std::string("Clx695"));
  CHECK_EQ(f.format_version, (int64_t)3);
  CHECK(f.capture_time.has_value());
  CHECK_EQ(f.capture_time->year, 2025);
  CHECK_EQ(f.capture_time->month, 7);
  CHECK_EQ(f.capture_time->day, 21);
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)916);
  CHECK_EQ(f.images[0].height(), (int64_t)733);
  CHECK_EQ(f.images[0].type(), (int64_t)3);
  CHECK_EQ(f.images[0].min_value(), (int64_t)500);
  CHECK_EQ(f.images[1].min_value(), (int64_t)1824);
  CHECK_EQ(f.images[1].byte_count(), f.images[1].width() * f.images[1].height() * 2);
  auto labels = f.channel_labels();
  CHECK_EQ(labels[0], std::string("brightfield"));
  CHECK_EQ(labels[1], std::string("fluorescence"));
}

TEST(core_samp4_metadata) {
  // Samp4 uses descriptor marker 0xC03D (others use 0xC03E). Descriptors are
  // identified by structural invariants, so the low byte variance is tolerated.
  clx_file f = load(data_dir() + "/Samp4_20260723_163251_00.00.946.clx");
  CHECK_EQ(f.magic, kMagic);
  CHECK_EQ(f.sample_name, std::string("Samp4_20260723_163251"));
  CHECK_EQ(f.exposure_ms, (int64_t)946);
  CHECK_EQ(f.software, std::string("Clx695"));
  CHECK_EQ(f.format_version, (int64_t)3);
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)687);
  CHECK_EQ(f.images[0].height(), (int64_t)550);
  CHECK_EQ(f.images[0].type(), (int64_t)4);
  CHECK_EQ(f.images[0].min_value(), (int64_t)160);
  CHECK_EQ(f.images[0].max_value(), (int64_t)65535);
  CHECK_EQ(f.images[1].min_value(), (int64_t)1218);
  CHECK_EQ(f.images[1].max_value(), (int64_t)32782);
  auto labels = f.channel_labels();
  CHECK_EQ(labels[0], std::string("brightfield"));
  CHECK_EQ(labels[1], std::string("fluorescence"));
}

TEST(core_samp5_metadata) {
  // Samp5 uses descriptor tag 0x403E (high byte 0x40, not 0xC0); regression test
  // for structural-invariant descriptor discovery.
  clx_file f = load(data_dir() + "/Samp5_20260813_110119_00.00.260.clx");
  CHECK_EQ(f.magic, kMagic);
  CHECK_EQ(f.sample_name, std::string("Samp5_20260813_110119"));
  CHECK_EQ(f.exposure_ms, (int64_t)260);
  CHECK_EQ(f.software, std::string("Clx695"));
  CHECK_EQ(f.format_version, (int64_t)3);
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)687);
  CHECK_EQ(f.images[0].height(), (int64_t)550);
  CHECK_EQ(f.images[0].type(), (int64_t)4);
  CHECK_EQ(f.images[0].min_value(), (int64_t)52);
  CHECK_EQ(f.images[0].max_value(), (int64_t)65535);
  CHECK_EQ(f.images[1].min_value(), (int64_t)1206);
  CHECK_EQ(f.images[1].max_value(), (int64_t)27752);
  auto labels = f.channel_labels();
  CHECK_EQ(labels[0], std::string("brightfield"));
  CHECK_EQ(labels[1], std::string("fluorescence"));
}

TEST(core_samp6_metadata) {
  // Samp6 is the largest capture (2750x2200, type 1) with a long exposure.
  clx_file f = load(data_dir() + "/Samp6_20250512_110653_01.23.279.clx");
  CHECK_EQ(f.magic, kMagic);
  CHECK_EQ(f.sample_name, std::string("Samp6_20250512_110653"));
  CHECK_EQ(f.exposure_ms, (int64_t)83279);
  CHECK_EQ(f.software, std::string("Clx695"));
  CHECK_EQ(f.format_version, (int64_t)3);
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)2750);
  CHECK_EQ(f.images[0].height(), (int64_t)2200);
  CHECK_EQ(f.images[0].type(), (int64_t)1);
  CHECK_EQ(f.images[0].min_value(), (int64_t)0);
  CHECK_EQ(f.images[0].max_value(), (int64_t)65535);
  CHECK_EQ(f.images[1].min_value(), (int64_t)0);
  CHECK_EQ(f.images[1].max_value(), (int64_t)7788);
  CHECK_EQ(f.images[1].byte_count(), f.images[1].width() * f.images[1].height() * 2);
  auto labels = f.channel_labels();
  CHECK_EQ(labels[0], std::string("brightfield"));
  CHECK_EQ(labels[1], std::string("fluorescence"));
}

TEST(core_samp7_metadata) {
  // Samp7 is a full-resolution (1375x1100, type 2) capture with a 3-part sample
  // stem; regression test for names containing extra underscores.
  clx_file f = load(data_dir() + "/Samp7_20260319_211250_00.07.453.clx");
  CHECK_EQ(f.magic, kMagic);
  CHECK_EQ(f.sample_name, std::string("Samp7_20260319_211250"));
  CHECK_EQ(f.exposure_ms, (int64_t)7453);
  CHECK_EQ(f.software, std::string("Clx695"));
  CHECK_EQ(f.format_version, (int64_t)3);
  CHECK_EQ(f.image_count(), (std::size_t)2);
  CHECK_EQ(f.images[0].width(), (int64_t)1375);
  CHECK_EQ(f.images[0].height(), (int64_t)1100);
  CHECK_EQ(f.images[0].type(), (int64_t)2);
  CHECK_EQ(f.images[0].min_value(), (int64_t)0);
  CHECK_EQ(f.images[0].max_value(), (int64_t)65535);
  CHECK_EQ(f.images[1].min_value(), (int64_t)0);
  CHECK_EQ(f.images[1].max_value(), (int64_t)51796);
  auto labels = f.channel_labels();
  CHECK_EQ(labels[0], std::string("brightfield"));
  CHECK_EQ(labels[1], std::string("fluorescence"));
}

TEST(core_descriptor_marker_variants) {
  // The leading 2-byte descriptor field is not a stable marker: 0xC03E, 0xC03D
  // and 0x403E have all been observed. Descriptor discovery must therefore rely
  // on structural invariants, not on the high byte being 0xC0.
  const uint16_t markers[] = {0xC03E, 0xC03D, 0x403E};
  for (uint16_t marker : markers) {
    auto data = make_synthetic_clx(marker, 687, 550, "S");
    clx_file f = parse(data, "synthetic.clx");
    CHECK_EQ(f.image_count(), (std::size_t)2);
    CHECK_EQ(f.images[0].width(), (int64_t)687);
    CHECK_EQ(f.images[0].height(), (int64_t)550);
    CHECK_EQ(f.images[0].byte_count(), (int64_t)687 * 550 * 2);
    CHECK_EQ(f.images[1].descriptor.offset,
             (int64_t)(0x400 + kDescriptorSize + 687 * 550 * 2));
  }
}

TEST(core_long_sample_name) {
  // The header sample name is a variable-length null-terminated string, not a
  // fixed-width field: a long capture stem (bounded by the Windows filename, not
  // by the file format) must be stored verbatim, not truncated.
  std::string name;
  for (int i = 0; i < 24; ++i) name += "abcdefgh";  // 192 chars
  name += "0123456789";                              // 202 chars total
  CHECK_EQ(name.size(), (std::size_t)202);
  auto data = make_synthetic_clx(0x403Eu, 2, 2, name);
  clx_file f = parse(data, "synthetic.clx");
  CHECK_EQ(f.sample_name, name);
  CHECK_EQ(f.image_count(), (std::size_t)2);
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
