#include "clxcpp/clx.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clxcpp {

namespace {

const std::set<std::string>& supported_formats() {
  static const std::set<std::string> s = {"tiff", "png", "json"};
  return s;
}

std::string path_stem(const std::string& path) {
  std::string name = path;
  std::size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  std::size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

void write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw format_error("cannot open file for writing: " + path);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void write_text_file(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  if (!out) throw format_error("cannot open file for writing: " + path);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace

std::map<std::string, std::string> image_metadata(const clx_file& f) {
  std::map<std::string, std::string> md;
  md["sample_name"] = f.sample_name;
  md["capture_time"] = f.capture_time ? f.capture_time->isoformat() : "";
  md["exposure_ms"] = std::to_string(f.exposure_ms);
  md["software"] = f.software;
  md["format_version"] = std::to_string(f.format_version);
  md["source_file"] = f.path;
  return md;
}

std::vector<std::string> export_images(
    const clx_file& f, const std::string& outdir,
    const std::vector<std::string>& formats_in, int dpi, bool preview,
    const std::optional<std::string>& prefix) {
  std::vector<std::string> formats;
  for (const auto& fmt : formats_in) {
    std::string lower;
    for (char c : fmt) lower.push_back(static_cast<char>(std::tolower(c)));
    formats.push_back(lower);
  }
  std::set<std::string> unk;
  for (const auto& fmt : formats) {
    if (!supported_formats().count(fmt)) unk.insert(fmt);
  }
  if (!unk.empty()) {
    std::ostringstream os;
    os << "unsupported format(s): ";
    bool first = true;
    for (const auto& u : unk) {
      if (!first) os << ", ";
      first = false;
      os << u;
    }
    throw std::invalid_argument(os.str());
  }

  std::error_code ec;
  bool existed = std::filesystem::exists(outdir, ec);
  if (!existed && !ec) {
    std::filesystem::create_directories(outdir, ec);
  }
  if (ec) {
    throw format_error("cannot create output directory: " + outdir);
  }

  std::string base = prefix ? *prefix : path_stem(f.path);
  std::filesystem::path dir(outdir);
  std::vector<std::string> written;

  for (const auto& img : f.images) {
    std::string stem = base + "_" + std::to_string(img.index) + "_" +
                       std::to_string(img.bits_per_sample()) + "bit";
    bool has_tiff = std::find(formats.begin(), formats.end(), "tiff") != formats.end();
    bool has_png = std::find(formats.begin(), formats.end(), "png") != formats.end();
    if (has_tiff) {
      std::string path = (dir / (stem + ".tif")).string();
      img.save_tiff(path, dpi);
      written.push_back(path);
    }
    if (has_png) {
      std::string path = (dir / (stem + ".png")).string();
      img.save_png(path, image_metadata(f));
      written.push_back(path);
    }
    if (preview) {
      std::string path =
          (dir / (base + "_" + std::to_string(img.index) + "_preview.png"))
              .string();
      write_file(path, img.preview_png_bytes(std::nullopt, std::nullopt,
                                             image_metadata(f)));
      written.push_back(path);
    }
  }

  bool has_json = std::find(formats.begin(), formats.end(), "json") != formats.end();
  if (has_json) {
    std::string path = (dir / (base + "_metadata.json")).string();
    write_text_file(path, f.to_json());
    written.push_back(path);
  }

  return written;
}

}  // namespace clxcpp
