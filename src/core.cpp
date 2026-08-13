#include "clxcpp/clx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace clxcpp {

namespace {

// --- little-endian readers -------------------------------------------------

uint32_t read_u32(const std::vector<uint8_t>& d, std::size_t off) {
  return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
         (static_cast<uint32_t>(d[off + 2]) << 16) |
         (static_cast<uint32_t>(d[off + 3]) << 24);
}

uint16_t read_u16(const std::vector<uint8_t>& d, std::size_t off) {
  return static_cast<uint16_t>(d[off]) | (static_cast<uint16_t>(d[off + 1]) << 8);
}

double read_f64(const std::vector<uint8_t>& d, std::size_t off) {
  uint64_t lo = read_u32(d, off);
  uint64_t hi = read_u32(d, off + 4);
  uint64_t bits = lo | (hi << 32);
  double v;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

// Read a null-padded ASCII string, mirroring _read_cstr: split at first NUL,
// decode as ASCII (invalid bytes -> '?'), strip ASCII whitespace.
std::string read_cstr(const std::vector<uint8_t>& d, std::size_t offset,
                      std::size_t length) {
  std::string out;
  out.reserve(length);
  for (std::size_t i = 0; i < length && offset + i < d.size(); ++i) {
    uint8_t c = d[offset + i];
    if (c == 0) break;
    out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  }
  // strip() of ASCII whitespace
  auto not_space = [](unsigned char c) {
    return c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\v' &&
           c != '\f';
  };
  auto b = std::find_if(out.begin(), out.end(), not_space);
  auto e = std::find_if(out.rbegin(), out.rend(), not_space).base();
  if (b >= e) return std::string();
  return std::string(b, e);
}

// Days from civil date to 1970-01-01 (Howard Hinnant's algorithm).
int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Civil date from days since 1970-01-01.
void civil_from_days(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yy + (m <= 2));
}

bool is_leap(int y) {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int days_in_month(int y, unsigned m) {
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && is_leap(y)) return 29;
  return dim[m - 1];
}

// Round half to even (banker's rounding), matching CPython's round().
double round_half_even(double x) {
  if (!std::isfinite(x)) return x;
  double r = std::floor(x);
  double frac = x - r;
  if (frac > 0.5) return r + 1.0;
  if (frac < 0.5) return r;
  // exactly .5: round to even
  if (std::fmod(r, 2.0) != 0.0) return r + 1.0;
  return r;
}

// Parse a strict YYYYMMDD[HHMM[SS]] string. Returns false on any invalid field.
bool parse_ymd_hms(const std::string& text, datetime& out) {
  if (text.size() != 8 && text.size() != 12 && text.size() != 14) return false;
  auto digit = [&](std::size_t i) { return text[i] >= '0' && text[i] <= '9'; };
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (!digit(i)) return false;
  }
  auto num = [&](std::size_t i, std::size_t n) {
    int v = 0;
    for (std::size_t k = 0; k < n; ++k) v = v * 10 + (text[i + k] - '0');
    return v;
  };
  int y = num(0, 4);
  int mo = num(4, 2);
  int d = num(6, 2);
  if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo)) return false;
  datetime dt;
  dt.year = y;
  dt.month = mo;
  dt.day = d;
  if (text.size() >= 12) {
    int h = num(8, 2);
    int mi = num(10, 2);
    if (h < 0 || h > 23 || mi < 0 || mi > 59) return false;
    dt.hour = h;
    dt.minute = mi;
  }
  if (text.size() >= 14) {
    int s = num(12, 2);
    if (s < 0 || s > 59) return false;
    dt.second = s;
  }
  out = dt;
  return true;
}

std::string pad2(int v) {
  char buf[3];
  buf[0] = static_cast<char>('0' + v / 10);
  buf[1] = static_cast<char>('0' + v % 10);
  buf[2] = 0;
  return std::string(buf, 2);
}

}  // namespace

std::string datetime::isoformat(char sep) const {
  std::ostringstream os;
  os << std::setw(4) << std::setfill('0') << year << '-'
     << pad2(month) << '-' << pad2(day) << sep << pad2(hour) << ':'
     << pad2(minute) << ':' << pad2(second);
  if (microsecond != 0) {
    os << '.' << std::setw(6) << std::setfill('0') << microsecond;
  }
  return os.str();
}

datetime ole_to_datetime(double value) {
  // total microseconds since 1899-12-30, rounded half-to-even like Python
  double total_us = round_half_even(value * 86400000000.0);
  int64_t total = static_cast<int64_t>(total_us);
  int64_t days = total / 86400000000LL;
  int64_t rem = total % 86400000000LL;
  int64_t seconds = rem / 1000000LL;
  int64_t micros = rem % 1000000LL;

  // OLE epoch 1899-12-30 = days_from_civil(1899,12,30) before 1970
  int64_t epoch = days_from_civil(1899, 12, 30);
  int y;
  unsigned m, d;
  civil_from_days(epoch + days, y, m, d);

  datetime out;
  out.year = y;
  out.month = static_cast<int>(m);
  out.day = static_cast<int>(d);
  out.hour = static_cast<int>(seconds / 3600);
  out.minute = static_cast<int>((seconds % 3600) / 60);
  out.second = static_cast<int>(seconds % 60);
  out.microsecond = static_cast<int>(micros);
  return out;
}

std::optional<datetime> parse_build_date(const std::string& text_in) {
  std::string text;
  text.reserve(text_in.size());
  for (char c : text_in) {
    if (c != '\x00') text.push_back(c);
  }
  datetime out;
  if (text.size() >= 12 && parse_ymd_hms(text.substr(0, 12), out)) return out;
  if (text.size() >= 8 && parse_ymd_hms(text.substr(0, 8), out)) return out;
  return std::nullopt;
}

std::optional<filename_info> parse_filename(const std::string& path) {
  std::string name = path;
  std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);

  // ^(.+?)_(\d{8})_(\d{6})_(\d{2}\.\d{2}\.\d{3})\.clx$
  static const std::regex re(
      R"(^(.+?)_(\d{8})_(\d{6})_(\d{2}\.\d{2}\.\d{3})\.clx$)");
  std::smatch m;
  if (!std::regex_match(name, m, re)) return std::nullopt;

  filename_info info;
  info.sample = m[1].str();
  info.date = m[2].str();
  info.time = m[3].str();

  std::string exp = m[4].str();  // MM.SS.mmm
  auto dot1 = exp.find('.');
  auto dot2 = exp.find('.', dot1 + 1);
  int minutes = std::stoi(exp.substr(0, dot1));
  int seconds = std::stoi(exp.substr(dot1 + 1, dot2 - dot1 - 1));
  int millis = std::stoi(exp.substr(dot2 + 1));
  info.exposure_ms = minutes * 60000LL + seconds * 1000LL + millis;

  datetime cap;
  if (parse_ymd_hms(info.date + info.time, cap)) info.capture_time = cap;

  return info;
}

std::optional<image_descriptor> parse_descriptor(const std::vector<uint8_t>& data,
                                                 std::size_t offset) {
  if (offset + kDescriptorSize > data.size()) return std::nullopt;

  // Identify a descriptor by its structural invariants alone. The leading
  // 2-byte field is not a reliable marker (high byte 0xC0/0x40, low byte
  // 0x3E/0x3D all observed), so it is deliberately ignored here. Check the most
  // selective fields first so a full-file scan stays cheap.
  uint32_t width = read_u32(data, offset + 6);
  if (width < 1 || width > kMaxDimension) return std::nullopt;
  uint32_t height = read_u32(data, offset + 10);
  if (height < 1 || height > kMaxDimension) return std::nullopt;
  uint32_t bits = read_u32(data, offset + 14);
  if (bits != 8 && bits != 16 && bits != 32) return std::nullopt;
  uint32_t mx = read_u32(data, offset + 18);
  uint32_t mn = read_u32(data, offset + 22);
  uint32_t byte_count = read_u32(data, offset + 26);
  if (byte_count != width * height * (bits / 8)) return std::nullopt;
  uint32_t full_scale = (uint32_t(1) << bits) - 1;
  if (mn > mx || mx > full_scale) return std::nullopt;
  if (offset + kDescriptorSize + byte_count > data.size()) return std::nullopt;

  uint32_t itype = read_u32(data, offset + 2);
  image_descriptor d;
  d.offset = static_cast<int64_t>(offset);
  d.type = itype;
  d.width = width;
  d.height = height;
  d.bits_per_sample = bits;
  d.max_value = mx;
  d.min_value = mn;
  d.byte_count = byte_count;
  return d;
}

std::vector<image_descriptor> find_descriptors(const std::vector<uint8_t>& data) {
  std::vector<image_descriptor> found;
  if (data.size() < kDescriptorSize) return found;
  for (std::size_t i = 0; i + kDescriptorSize <= data.size(); ++i) {
    auto desc = parse_descriptor(data, i);
    if (desc) found.push_back(*desc);
  }
  return found;
}

trailer_info parse_trailer_info(const std::vector<uint8_t>& trailer,
                                int64_t exposure_ms) {
  trailer_info info;
  if (trailer.size() >= 32) {
    info.field_0 = read_u32(trailer, 0);
    info.full_scale = read_u32(trailer, 4);
    info.type_0 = read_u32(trailer, 8);
    info.type_1 = read_u32(trailer, 12);
    info.exposure_ms = read_u32(trailer, 16);
    info.max_value = read_u32(trailer, 20);
    info.type_2 = read_u32(trailer, 24);
    info.type_3 = read_u32(trailer, 28);
    info.exposure_ms_matches_header = info.exposure_ms == exposure_ms;
  }
  auto find_cstr = [&](const std::string& needle) -> bool {
    for (std::size_t i = 0; i + needle.size() <= trailer.size(); ++i) {
      bool ok = true;
      for (std::size_t k = 0; k < needle.size(); ++k) {
        if (trailer[i + k] != static_cast<uint8_t>(needle[k])) {
          ok = false;
          break;
        }
      }
      if (ok) {
        std::string text = read_cstr(trailer, i, 64);
        if (needle == "Gray.pal") {
          info.has_gray_pal = true;
          info.gray_pal = text;
        } else {
          info.has_build_date = true;
          info.build_date_text = text;
        }
        return true;
      }
    }
    return false;
  };
  find_cstr("Gray.pal");
  find_cstr(kBuildDateString);
  return info;
}

std::map<int, std::string> clx_file::channel_labels() const {
  std::map<int, std::string> out;
  if (images.size() != 2) return out;
  out[0] = "brightfield";
  out[1] = "fluorescence";
  return out;
}

std::string clx_file::summary() const {
  std::ostringstream os;
  auto dt_str = [](const std::optional<datetime>& v) {
    return v ? v->isoformat() : "-";
  };
  os << "File            : " << path << "\n";
  os << "Sample          : " << (sample_name.empty() ? "-" : sample_name) << "\n";
  os << "Captured        : " << dt_str(capture_time) << "\n";
  os << "Exposure        : " << exposure_ms << " ms\n";
  os << "Software        : " << software << " (format v" << format_version
     << ")\n";
  os << "Build date      : " << dt_str(build_datetime) << "\n";
  os << "Images          : " << image_count() << "\n";
  if (filename_info_) {
    os << "Filename meta   : sample='" << filename_info_->sample
       << "' date=" << filename_info_->date << " time=" << filename_info_->time
       << " exposure=" << filename_info_->exposure_ms << " ms\n";
  }
  auto labels = channel_labels();
  for (const auto& img : images) {
    std::string hint;
    auto it = labels.find(img.index);
    if (it != labels.end()) hint = it->second;
    os << "  - [" << img.index << "] " << img.width() << "x" << img.height()
       << " " << img.bits_per_sample() << "-bit type=" << img.type()
       << " min=" << img.min_value() << " max=" << img.max_value();
    if (!hint.empty()) os << " " << hint;
    os << "\n";
  }
  os << "Trailer         : " << trailer.size() << " bytes";
  return os.str();
}

namespace {

// Ordered key-value JSON writer matching Python's json.dumps(indent=2).
class json_value {
 public:
  enum class type { str, num, boolean, null, object, array };

  type t = type::null;
  std::string s;
  int64_t n = 0;
  bool b = false;
  std::vector<std::pair<std::string, json_value>> obj;
  std::vector<json_value> arr;

  static json_value str_value(std::string v) {
    json_value j;
    j.t = type::str;
    j.s = std::move(v);
    return j;
  }
  static json_value num_value(int64_t v) {
    json_value j;
    j.t = type::num;
    j.n = v;
    return j;
  }
  static json_value bool_value(bool v) {
    json_value j;
    j.t = type::boolean;
    j.b = v;
    return j;
  }
  static json_value null_value() { return json_value{}; }
  static json_value obj_value() {
    json_value j;
    j.t = type::object;
    return j;
  }
  static json_value arr_value() {
    json_value j;
    j.t = type::array;
    return j;
  }
};

void json_escape(std::ostringstream& os, const std::string& s) {
  os << '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': os << "\\\""; break;
      case '\\': os << "\\\\"; break;
      case '\b': os << "\\b"; break;
      case '\f': os << "\\f"; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          os << buf;
        } else {
          os << static_cast<char>(c);
        }
    }
  }
  os << '"';
}

void json_write(std::ostringstream& os, const json_value& j, int indent,
                int level) {
  switch (j.t) {
    case json_value::type::str:
      json_escape(os, j.s);
      break;
    case json_value::type::num:
      os << j.n;
      break;
    case json_value::type::boolean:
      os << (j.b ? "true" : "false");
      break;
    case json_value::type::null:
      os << "null";
      break;
    case json_value::type::object: {
      if (j.obj.empty()) {
        os << "{}";
        break;
      }
      os << "{\n";
      for (std::size_t i = 0; i < j.obj.size(); ++i) {
        os << std::string(static_cast<std::size_t>((level + 1) * indent), ' ');
        json_escape(os, j.obj[i].first);
        os << ": ";
        json_write(os, j.obj[i].second, indent, level + 1);
        if (i + 1 < j.obj.size()) os << ',';
        os << '\n';
      }
      os << std::string(static_cast<std::size_t>(level * indent), ' ') << '}';
      break;
    }
    case json_value::type::array: {
      if (j.arr.empty()) {
        os << "[]";
        break;
      }
      os << "[\n";
      for (std::size_t i = 0; i < j.arr.size(); ++i) {
        os << std::string(static_cast<std::size_t>((level + 1) * indent), ' ');
        json_write(os, j.arr[i], indent, level + 1);
        if (i + 1 < j.arr.size()) os << ',';
        os << '\n';
      }
      os << std::string(static_cast<std::size_t>(level * indent), ' ') << ']';
      break;
    }
  }
}

}  // namespace

std::string clx_file::to_json() const {
  auto dt_str = [](const std::optional<datetime>& v) -> json_value {
    return v ? json_value::str_value(v->isoformat()) : json_value::null_value();
  };

  json_value images_arr = json_value::arr_value();
  for (const auto& img : images) {
    json_value d = json_value::obj_value();
    d.obj.emplace_back("offset", json_value::num_value(img.descriptor.offset));
    d.obj.emplace_back("type", json_value::num_value(img.type()));
    d.obj.emplace_back("width", json_value::num_value(img.width()));
    d.obj.emplace_back("height", json_value::num_value(img.height()));
    d.obj.emplace_back("bits_per_sample", json_value::num_value(img.bits_per_sample()));
    d.obj.emplace_back("min_value", json_value::num_value(img.min_value()));
    d.obj.emplace_back("max_value", json_value::num_value(img.max_value()));
    d.obj.emplace_back("byte_count", json_value::num_value(img.byte_count()));
    d.obj.emplace_back("index", json_value::num_value(img.index));
    images_arr.arr.push_back(std::move(d));
  }

  json_value root = json_value::obj_value();
  root.obj.emplace_back("file", json_value::str_value(path));
  root.obj.emplace_back("magic", json_value::num_value(magic));
  root.obj.emplace_back("format_version", json_value::num_value(format_version));
  root.obj.emplace_back("software", json_value::str_value(software));
  root.obj.emplace_back("build_datetime", dt_str(build_datetime));
  root.obj.emplace_back("sample_name", json_value::str_value(sample_name));
  root.obj.emplace_back("capture_time", dt_str(capture_time));
  root.obj.emplace_back("exposure_ms", json_value::num_value(exposure_ms));

  if (filename_info_) {
    json_value fi = json_value::obj_value();
    fi.obj.emplace_back("sample", json_value::str_value(filename_info_->sample));
    fi.obj.emplace_back("date", json_value::str_value(filename_info_->date));
    fi.obj.emplace_back("time", json_value::str_value(filename_info_->time));
    fi.obj.emplace_back("exposure_ms",
                       json_value::num_value(filename_info_->exposure_ms));
    fi.obj.emplace_back("capture_time", dt_str(filename_info_->capture_time));
    root.obj.emplace_back("filename_info", std::move(fi));
  } else {
    root.obj.emplace_back("filename_info", json_value::null_value());
  }

  root.obj.emplace_back("image_count", json_value::num_value(image_count()));
  root.obj.emplace_back("image_type",
                        image_type() ? json_value::num_value(*image_type())
                                     : json_value::null_value());
  root.obj.emplace_back("images", std::move(images_arr));
  root.obj.emplace_back("trailer_size", json_value::num_value(trailer.size()));

  json_value ti = json_value::obj_value();
  ti.obj.emplace_back("field_0", json_value::num_value(raw_trailer_info_.field_0));
  ti.obj.emplace_back("full_scale",
                      json_value::num_value(raw_trailer_info_.full_scale));
  ti.obj.emplace_back("type_0", json_value::num_value(raw_trailer_info_.type_0));
  ti.obj.emplace_back("type_1", json_value::num_value(raw_trailer_info_.type_1));
  ti.obj.emplace_back("exposure_ms",
                      json_value::num_value(raw_trailer_info_.exposure_ms));
  ti.obj.emplace_back("max_value",
                      json_value::num_value(raw_trailer_info_.max_value));
  ti.obj.emplace_back("type_2", json_value::num_value(raw_trailer_info_.type_2));
  ti.obj.emplace_back("type_3", json_value::num_value(raw_trailer_info_.type_3));
  ti.obj.emplace_back(
      "exposure_ms_matches_header",
      json_value::bool_value(raw_trailer_info_.exposure_ms_matches_header));
  if (raw_trailer_info_.has_gray_pal) {
    ti.obj.emplace_back("Gray.pal",
                        json_value::str_value(raw_trailer_info_.gray_pal));
  }
  if (raw_trailer_info_.has_build_date) {
    ti.obj.emplace_back(kBuildDateString,
                        json_value::str_value(raw_trailer_info_.build_date_text));
  }
  root.obj.emplace_back("trailer_info", std::move(ti));

  std::ostringstream os;
  json_write(os, root, 2, 0);
  return os.str();
}

clx_file parse(const std::vector<uint8_t>& data, const std::string& path) {
  if (data.size() < kHeaderSize + kDescriptorSize) {
    throw format_error("file too small to be a .clx capture");
  }

  uint32_t magic = read_u32(data, 0);
  if (magic != kMagic) {
    std::ostringstream os;
    os << "bad magic 0x" << std::uppercase << std::hex << std::setw(8)
       << std::setfill('0') << magic;
    if (data.size() >= 4 &&
        ((data[0] == 'I' && data[1] == 'I' && data[2] == '*' && data[3] == 0) ||
         (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == '*'))) {
      throw format_error("input looks like a TIFF image, not a .clx capture (" +
                         os.str() + ")");
    }
    throw format_error("not a .clx file (" + os.str() + ")");
  }

  clx_file f;
  f.path = path;
  f.magic = magic;
  f.capture_time = ole_to_datetime(read_f64(data, 0x0C));
  f.exposure_ms = read_u32(data, 0x14);
  f.sample_name = read_cstr(data, 0x18, kSampleNameMaxSize);
  f.format_version = read_u32(data, 0x0124);
  f.software = read_cstr(data, 0x0128, 0x100);
  std::string build_date_text = read_cstr(data, 0x0228, 0x100);
  f.build_datetime = parse_build_date(build_date_text);

  auto descriptors = find_descriptors(data);
  if (descriptors.empty()) {
    throw format_error("no valid image descriptors found in file");
  }

  for (std::size_t index = 0; index < descriptors.size(); ++index) {
    const auto& desc = descriptors[index];
    clx_image img;
    img.index = static_cast<int>(index);
    img.descriptor = desc;
    std::size_t start = static_cast<std::size_t>(desc.pixel_offset());
    img.pixel_buf.assign(data.begin() + static_cast<std::ptrdiff_t>(start),
                         data.begin() +
                             static_cast<std::ptrdiff_t>(start + desc.byte_count));
    f.images.push_back(std::move(img));
  }

  const auto& last = descriptors.back();
  std::size_t last_end =
      static_cast<std::size_t>(last.pixel_offset() + last.byte_count);
  f.trailer.assign(data.begin() + static_cast<std::ptrdiff_t>(last_end),
                   data.end());
  f.raw_header.assign(data.begin(),
                      data.begin() + static_cast<std::ptrdiff_t>(kHeaderSize));
  f.raw_trailer_info_ = parse_trailer_info(f.trailer, f.exposure_ms);
  f.filename_info_ = parse_filename(path);

  return f;
}

clx_file load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw format_error("cannot open file: " + path);
  }
  in.seekg(0, std::ios::end);
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<std::size_t>(size));
  if (size > 0) {
    in.read(reinterpret_cast<char*>(data.data()), size);
  }
  if (!in && size > 0) {
    throw format_error("error reading file: " + path);
  }
  return parse(data, path);
}

}  // namespace clxcpp
