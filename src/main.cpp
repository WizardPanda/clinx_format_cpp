#include "clxcpp/clx.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct options {
  std::string command;
  std::vector<std::string> files;
  std::string outdir = ".";
  std::string formats = "tiff,png,json";
  bool preview = false;
  long long dpi = 600;
  std::string out;
  long long image = 0;
  bool has_low = false;
  bool has_high = false;
  long long low = 0;
  long long high = 0;
};

void print_usage(const char* prog) {
  std::cerr
      << "usage: " << prog
      << " info|extract|preview <file.clx> [options]\n"
      << "\n"
      << "Parse and extract Clinx chemiluminescence instrument .clx captures\n"
      << "(metadata + 16-bit fluorescence and bright-field images).\n"
      << "\n"
      << "commands:\n"
      << "  info <file...>              show metadata for one or more .clx files\n"
      << "  extract <file...> [opts]    export images and metadata\n"
      << "  preview <file> [opts]       write an 8-bit preview PNG of one image\n"
      << "\n"
      << "extract options:\n"
      << "  -o, --outdir DIR            output directory (default: .)\n"
      << "      --formats FMT[,FMT...]  tiff,png,json (default: tiff,png,json)\n"
      << "      --preview               also write 8-bit auto-scaled previews\n"
      << "      --dpi N                 TIFF resolution (default: 600)\n"
      << "\n"
      << "preview options:\n"
      << "  -o, --out PATH              output path (default: <name>_preview.png)\n"
      << "      --image N               image index to preview (default: 0)\n"
      << "      --low N                 stretch lower bound (default: 1st pctl)\n"
      << "      --high N                stretch upper bound (default: 99th pctl)\n";
}

bool parse_int(const std::string& s, long long& out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '-' || s[0] == '+') i = 1;
  if (i == s.size()) return false;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  out = std::stoll(s);
  return true;
}

int run_info(const std::vector<std::string>& files) {
  for (const auto& path : files) {
    clxcpp::clx_file f = clxcpp::load(path);
    std::cout << f.summary() << "\n\n";
  }
  return 0;
}

int run_extract(const std::vector<std::string>& files, const options& o) {
  std::vector<std::string> formats;
  std::string cur;
  for (char c : o.formats) {
    if (c == ',') {
      if (!cur.empty()) formats.push_back(cur);
      cur.clear();
    } else if (c != ' ') {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) formats.push_back(cur);

  for (const auto& path : files) {
    clxcpp::clx_file f = clxcpp::load(path);
    auto written = clxcpp::export_images(f, o.outdir, formats, o.dpi, o.preview);
    for (const auto& w : written) std::cout << w << "\n";
  }
  return 0;
}

int run_preview(const std::string& file, const options& o) {
  clxcpp::clx_file f = clxcpp::load(file);
  if (!(0 <= o.image && o.image < static_cast<int>(f.images.size()))) {
    throw clxcpp::format_error(
        "image index " + std::to_string(o.image) + " out of range (file has " +
        std::to_string(f.images.size()) + " images)");
  }
  const clxcpp::clx_image& img = f.images[static_cast<std::size_t>(o.image)];

  std::string out = o.out;
  if (out.empty()) {
    std::string name = file;
    std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    out = name + "_preview.png";
  }

  std::optional<long long> low, high;
  if (o.has_low) low = o.low;
  if (o.has_high) high = o.high;
  std::optional<int64_t> ilow, ihigh;
  if (low) ilow = *low;
  if (high) ihigh = *high;

  auto bytes = img.preview_png_bytes(ilow, ihigh, clxcpp::image_metadata(f));
  std::ofstream out_file(out, std::ios::binary);
  if (!out_file) throw clxcpp::format_error("cannot open file for writing: " + out);
  out_file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  std::cout << out << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 2;
  }

  options o;
  o.command = argv[1];

  int i = 2;
  try {
    if (o.command == "help" || o.command == "-h" || o.command == "--help") {
      print_usage(argv[0]);
      return 0;
    }

    if (o.command == "info") {
      for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
          print_usage(argv[0]);
          return 0;
        }
        if (!a.empty() && a[0] == '-' && a != "-") {
          std::cerr << "error: unexpected option: " << a << "\n";
          return 2;
        }
        o.files.push_back(a);
      }
      if (o.files.empty()) {
        std::cerr << "error: info requires at least one file\n";
        return 2;
      }
      return run_info(o.files);
    }

    if (o.command == "extract") {
      for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" || a == "--outdir") {
          if (i + 1 >= argc) {
            std::cerr << "error: " << a << " requires an argument\n";
            return 2;
          }
          o.outdir = argv[++i];
        } else if (a == "--formats") {
          if (i + 1 >= argc) {
            std::cerr << "error: --formats requires an argument\n";
            return 2;
          }
          o.formats = argv[++i];
        } else if (a == "--preview") {
          o.preview = true;
        } else if (a == "--dpi") {
          if (i + 1 >= argc || !parse_int(argv[i + 1], o.dpi)) {
            std::cerr << "error: --dpi requires an integer argument\n";
            return 2;
          }
          ++i;
        } else if (a == "-h" || a == "--help") {
          print_usage(argv[0]);
          return 0;
        } else if (!a.empty() && a[0] == '-') {
          std::cerr << "error: unexpected option: " << a << "\n";
          return 2;
        } else {
          o.files.push_back(a);
        }
      }
      if (o.files.empty()) {
        std::cerr << "error: extract requires at least one file\n";
        return 2;
      }
      return run_extract(o.files, o);
    }

    if (o.command == "preview") {
      std::string file;
      for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" || a == "--out") {
          if (i + 1 >= argc) {
            std::cerr << "error: " << a << " requires an argument\n";
            return 2;
          }
          o.out = argv[++i];
        } else if (a == "--image") {
          if (i + 1 >= argc || !parse_int(argv[i + 1], o.image)) {
            std::cerr << "error: --image requires an integer argument\n";
            return 2;
          }
          ++i;
        } else if (a == "--low") {
          if (i + 1 >= argc || !parse_int(argv[i + 1], o.low)) {
            std::cerr << "error: --low requires an integer argument\n";
            return 2;
          }
          o.has_low = true;
          ++i;
        } else if (a == "--high") {
          if (i + 1 >= argc || !parse_int(argv[i + 1], o.high)) {
            std::cerr << "error: --high requires an integer argument\n";
            return 2;
          }
          o.has_high = true;
          ++i;
        } else if (a == "-h" || a == "--help") {
          print_usage(argv[0]);
          return 0;
        } else if (!a.empty() && a[0] == '-') {
          std::cerr << "error: unexpected option: " << a << "\n";
          return 2;
        } else {
          if (!file.empty()) {
            std::cerr << "error: preview takes a single file\n";
            return 2;
          }
          file = a;
        }
      }
      if (file.empty()) {
        std::cerr << "error: preview requires a file\n";
        return 2;
      }
      return run_preview(file, o);
    }

    std::cerr << "error: unknown command: " << o.command << "\n";
    return 2;
  } catch (const clxcpp::format_error& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
