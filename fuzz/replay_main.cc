// Runs a fuzz target over files without libFuzzer: each argument is a file,
// or a directory whose regular files are run in name order. ctest uses it to
// replay every target's corpus in the ordinary build (and under ASan/UBSan in
// native-asan), so a regression in a parser shows without a fuzzing job. As
// libwebrtc's test/fuzzers build a plain executable from the same target when
// libFuzzer is not in use.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) {
  std::vector<std::filesystem::path> files;
  for (int i = 1; i < argc; ++i) {
    const std::filesystem::path p(argv[i]);
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) {
      for (const auto& e : std::filesystem::directory_iterator(p)) {
        if (e.is_regular_file()) files.push_back(e.path());
      }
    } else if (std::filesystem::is_regular_file(p, ec)) {
      files.push_back(p);
    } else {
      std::fprintf(stderr, "%s: no such file or directory\n", argv[i]);
      return 2;
    }
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    std::fprintf(stderr, "no inputs\n");
    return 2;
  }
  for (const auto& f : files) {
    std::ifstream in(f, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
  }
  // The empty input too, as libFuzzer always runs it.
  LLVMFuzzerTestOneInput(nullptr, 0);
  std::printf("%zu inputs ok\n", files.size());
  return 0;
}
