// Fuzz target for the text parser (the only code that handles untrusted input).
// Besides "doesn't crash / no UB", it checks the parser's contract on whatever it accepts.

#include "ome/text_format.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

void require(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "parse_fuzz: violated: %s\n", what);
        std::abort();
    }
}

bool hasWhitespace(std::string_view s) {
    return s.find_first_of(" \t\r\n") != std::string_view::npos;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    if (const auto price = ome::text::parsePrice(text)) {
        require(*price > 0, "accepted price is positive");
        require(ome::text::parsePrice(ome::text::formatPrice(*price)) == price,
                "format(parse(x)) parses back to the same ticks");
    }

    const auto line = ome::text::parseOrderLine(text);
    using Status = ome::text::LineResult::Status;
    switch (line.status) {
        case Status::Ok:
            require(!line.order.id.empty() && !hasWhitespace(line.order.id), "id is one token");
            require(line.order.quantity > 0, "quantity is positive");
            require(!line.order.price || *line.order.price > 0, "price is positive");
            break;
        case Status::Error: require(!line.error.empty(), "errors have a message"); break;
        case Status::Blank:
            require(text.find_first_not_of(" \t\r\n") == std::string_view::npos,
                    "only whitespace is blank");
            break;
    }
    return 0;
}
