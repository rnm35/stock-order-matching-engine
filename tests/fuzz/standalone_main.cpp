// A main() for the fuzz targets when libFuzzer isn't available (e.g. GCC), so they still
// build and run in the normal test suite. It is not coverage-guided: it replays any files
// given on the command line, then feeds N random inputs built from bytes that are
// "interesting" to the parsers (digits, '.', spaces, B/S, ...) mixed with arbitrary bytes.
//
//   <target> [-runs=N] [-seed=S] [files or corpus directories...]

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

bool replayFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot read " << path << '\n';
        return false;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), {}};
    LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return true;
}

int run(int argc, char** argv) {
    std::uint64_t runs = 100'000;
    std::uint64_t state = 1;
    std::size_t replayed = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.starts_with("-runs=")) {
            runs = std::strtoull(arg.c_str() + 6, nullptr, 10);
        } else if (arg.starts_with("-seed=")) {
            state = std::strtoull(arg.c_str() + 6, nullptr, 10);
        } else if (std::filesystem::is_directory(arg)) {
            for (const auto& entry : std::filesystem::directory_iterator(arg)) {
                if (!entry.is_regular_file()) continue;
                if (!replayFile(entry.path())) return 1;
                ++replayed;
            }
        } else {
            if (!replayFile(arg)) return 1;
            ++replayed;
        }
    }

    auto next = [&state] {  // splitmix64
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    };
    static constexpr std::string_view kAlphabet = "0123456789012345.. \t\r\nBSBSbsxo-+e";
    std::vector<std::uint8_t> buf;
    for (std::uint64_t r = 0; r < runs; ++r) {
        buf.resize(next() % 64);
        for (auto& b : buf) {
            const auto x = next();
            b = x % 4 == 0 ? static_cast<std::uint8_t>(x >> 8)
                           : static_cast<std::uint8_t>(kAlphabet[(x >> 8) % kAlphabet.size()]);
        }
        LLVMFuzzerTestOneInput(buf.data(), buf.size());
    }
    std::cout << "ok: " << replayed << " corpus files, " << runs << " random inputs\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "fuzz driver: " << e.what() << '\n';
        return 1;
    }
}
