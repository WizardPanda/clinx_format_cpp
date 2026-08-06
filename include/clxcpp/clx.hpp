#ifndef CLXCPP_CLX_HPP
#define CLXCPP_CLX_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace clxcpp {

// Format constants (little-endian throughout). See docs/clx-format-spec.md.
constexpr uint32_t kMagic = 0x000025EB;
constexpr std::size_t kHeaderSize = 0x0124;
constexpr std::size_t kVersionBlockSize = 0x0104;
constexpr std::size_t kBuildDateSize = 0x0100;
constexpr std::size_t kDescriptorSize = 34;
constexpr uint16_t kDescriptorMarker = 0xC03E;
constexpr int64_t kMaxDimension = 8192;
constexpr const char* kBuildDateString = "202312281113";

class format_error : public std::runtime_error {
 public:
  explicit format_error(const std::string& msg) : std::runtime_error(msg) {}
};

// A naive date-time (no timezone), mirroring Python's datetime.
struct datetime {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int microsecond = 0;

  bool operator==(const datetime& o) const {
    return year == o.year && month == o.month && day == o.day &&
           hour == o.hour && minute == o.minute && second == o.second &&
           microsecond == o.microsecond;
  }
  bool operator!=(const datetime& o) const { return !(*this == o); }

  // Equivalent to Python's datetime.isoformat(sep=' ').
  std::string isoformat(char sep = ' ') const;
};

// A validated 34-byte image descriptor found inside the file.
struct image_descriptor {
  int64_t offset = 0;
  int64_t type = 0;
  int64_t width = 0;
  int64_t height = 0;
  int64_t bits_per_sample = 0;
  int64_t max_value = 0;
  int64_t min_value = 0;
  int64_t byte_count = 0;

  int64_t pixel_offset() const { return offset + kDescriptorSize; }
};

// Best-effort decode of the trailing settings/statistics block.
struct trailer_info {
  bool has_fields = false;
  uint32_t field_0 = 0;
  uint32_t full_scale = 0;
  uint32_t type_0 = 0;
  uint32_t type_1 = 0;
  uint32_t exposure_ms = 0;
  uint32_t max_value = 0;
  uint32_t type_2 = 0;
  uint32_t type_3 = 0;
  bool exposure_ms_matches_header = false;
  std::string gray_pal;         // "Gray.pal" string found in trailer, if any
  std::string build_date_text;  // build-date string found in trailer, if any
  bool has_gray_pal = false;
  bool has_build_date = false;
};

// Structured info extracted from an instrument-style .clx filename.
struct filename_info {
  std::string sample;
  std::string date;
  std::string time;
  int64_t exposure_ms = 0;
  std::optional<datetime> capture_time;
};

class clx_file;

// A single image (bright field or fluorescence) embedded in a .clx.
class clx_image {
 public:
  int index = 0;
  image_descriptor descriptor;
  std::vector<uint8_t> pixel_buf;  // raw byte_count bytes, uint16 LE row-major

  int64_t type() const { return descriptor.type; }
  int64_t width() const { return descriptor.width; }
  int64_t height() const { return descriptor.height; }
  int64_t bits_per_sample() const { return descriptor.bits_per_sample; }
  int64_t min_value() const { return descriptor.min_value; }
  int64_t max_value() const { return descriptor.max_value; }
  int64_t byte_count() const { return descriptor.byte_count; }
  int64_t pixel_offset() const { return descriptor.pixel_offset(); }

  // TIFF byte string whose pixel strip is exactly this image's raw pixels.
  std::vector<uint8_t> to_tiff_bytes(int dpi = 600) const;

  // Lossless 16-bit grayscale PNG (or 8-bit when bits_per_sample == 8).
  std::vector<uint8_t> to_png_bytes(
      const std::map<std::string, std::string>& metadata = {}) const;

  // 8-bit auto-scaled preview PNG, replicating the numpy percentile scaling.
  std::vector<uint8_t> preview_png_bytes(
      std::optional<int64_t> low = std::nullopt,
      std::optional<int64_t> high = std::nullopt,
      const std::map<std::string, std::string>& metadata = {}) const;

  void save_tiff(const std::string& path, int dpi = 600) const;
  void save_png(const std::string& path,
                const std::map<std::string, std::string>& metadata = {}) const;
};

// A parsed .clx file: metadata plus embedded images.
class clx_file {
 public:
  std::string path;
  uint32_t magic = 0;
  int64_t format_version = 0;
  std::string software;
  std::optional<datetime> build_datetime;
  std::string sample_name;
  std::optional<datetime> capture_time;
  int64_t exposure_ms = 0;
  std::optional<filename_info> filename_info_;
  std::vector<clx_image> images;
  std::vector<uint8_t> trailer;
  std::vector<uint8_t> raw_header;
  trailer_info raw_trailer_info_;

  std::size_t image_count() const { return images.size(); }
  std::optional<int64_t> image_type() const {
    if (images.empty()) return std::nullopt;
    return images[0].type();
  }

  // Channel labels for 2-image captures: {0: "brightfield", 1: "fluorescence"},
  // matching the instrument's stable image order.
  std::map<int, std::string> channel_labels() const;

  // Human-readable summary, matching clxparser's ClxFile.summary().
  std::string summary() const;

  // JSON-serializable metadata dict (matches clxparser's to_dict()).
  std::string to_json() const;
};

// Parse raw .clx bytes into a clx_file. Throws format_error on failure.
clx_file parse(const std::vector<uint8_t>& data, const std::string& path = "");

// Read and parse a .clx file from disk.
clx_file load(const std::string& path);

// Helpers exposed for testing / parity checks.
datetime ole_to_datetime(double value);
std::optional<datetime> parse_build_date(const std::string& text);
std::optional<filename_info> parse_filename(const std::string& path);
std::vector<image_descriptor> find_descriptors(const std::vector<uint8_t>& data);
std::optional<image_descriptor> parse_descriptor(const std::vector<uint8_t>& data,
                                                 std::size_t offset);
trailer_info parse_trailer_info(const std::vector<uint8_t>& trailer,
                                int64_t exposure_ms);

// Low-level encoders (used by the export helpers and CLI).
std::vector<uint8_t> encode_tiff(int64_t width, int64_t height,
                                 int64_t bits_per_sample,
                                 const std::vector<uint8_t>& pixels,
                                 int dpi = 600, int photometric = 1);
std::vector<uint8_t> encode_png(int64_t width, int64_t height, int bits,
                                const std::vector<uint8_t>& pixel_bytes_le,
                                const std::map<std::string, std::string>& metadata = {});

// Bulk export helpers.
std::vector<std::string> export_images(
    const clx_file& f, const std::string& outdir,
    const std::vector<std::string>& formats, int dpi = 600, bool preview = false,
    const std::optional<std::string>& prefix = std::nullopt);
std::map<std::string, std::string> image_metadata(const clx_file& f);

}  // namespace clxcpp

#endif  // CLXCPP_CLX_HPP
