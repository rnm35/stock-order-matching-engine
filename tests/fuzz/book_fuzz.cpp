// Coverage-guided differential fuzzing: the fuzzer's bytes become a sequence of book
// operations (see decodeOps), which are run through OrderBook and ReferenceBook. Any
// difference in events or final state, or a broken invariant, aborts.

#include "differential.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const auto mode = (data[0] & 1U) != 0 ? ome::MatchingMode::Modern : ome::MatchingMode::Legacy;
    ome::test::DifferentialRunner runner({mode, ome::Price{10'000}});

    const auto ops = ome::test::decodeOps(data + 1, size - 1);
    for (std::size_t i = 0; i < ops.size(); ++i) {
        auto diff = runner.apply(ops[i]);
        if (diff.empty()) diff = runner.checkState();
        if (!diff.empty()) {
            std::cerr << "book_fuzz: mismatch in " << ome::toString(mode) << " mode at op " << i
                      << ": " << ops[i].describe() << '\n'
                      << diff;
            std::abort();
        }
    }
    return 0;
}
