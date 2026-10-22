#include "ome/text_format.hpp"

#include <charconv>
#include <cstdint>
#include <vector>

namespace ome::text {
namespace {

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool isSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> splitWhitespace(std::string_view s) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isSpace(s[i])) ++i;
        const std::size_t start = i;
        while (i < s.size() && !isSpace(s[i])) ++i;
        if (i > start) tokens.push_back(s.substr(start, i - start));
    }
    return tokens;
}

}  // namespace

std::optional<Price> parsePrice(std::string_view text) {
    // 15 integer digits * 100 ticks still fits comfortably in int64.
    constexpr int kMaxIntegerDigits = 15;

    std::size_t i = 0;
    Price whole = 0;
    int integerDigits = 0;
    for (; i < text.size() && isDigit(text[i]); ++i) {
        if (++integerDigits > kMaxIntegerDigits) return std::nullopt;
        whole = whole * 10 + (text[i] - '0');
    }

    Price cents = 0;
    int fractionDigits = 0;
    if (i < text.size() && text[i] == '.') {
        for (++i; i < text.size() && isDigit(text[i]); ++i, ++fractionDigits) {
            if (fractionDigits < 2) {
                cents = cents * 10 + (text[i] - '0');
            } else if (text[i] != '0') {
                return std::nullopt;  // finer than one tick
            }
        }
    }
    if (fractionDigits == 1) cents *= 10;  // "100.5" means 50 cents

    if (i != text.size() || integerDigits + fractionDigits == 0) return std::nullopt;
    const Price ticks = whole * kTicksPerUnit + cents;
    if (ticks <= 0) return std::nullopt;
    return ticks;
}

std::string formatPrice(Price ticks) {
    const bool negative = ticks < 0;
    const std::uint64_t magnitude =
        negative ? 0 - static_cast<std::uint64_t>(ticks) : static_cast<std::uint64_t>(ticks);
    const std::uint64_t cents = magnitude % kTicksPerUnit;

    std::string out = negative ? "-" : "";
    out += std::to_string(magnitude / kTicksPerUnit);
    out += '.';
    out += static_cast<char>('0' + cents / 10);
    out += static_cast<char>('0' + cents % 10);
    return out;
}

LineResult parseOrderLine(std::string_view line) {
    LineResult result;
    const auto tokens = splitWhitespace(line);
    if (tokens.empty()) return result;  // Blank

    result.status = LineResult::Status::Error;
    if (tokens.size() < 3 || tokens.size() > 4) {
        result.error = "expected <order_id> <B|S> <quantity> [price]";
        return result;
    }

    if (tokens[1] == "B") {
        result.order.side = Side::Buy;
    } else if (tokens[1] == "S") {
        result.order.side = Side::Sell;
    } else {
        result.error = "order type must be B or S";
        return result;
    }

    const std::string_view qty = tokens[2];
    Quantity quantity = 0;
    const auto [end, ec] = std::from_chars(qty.data(), qty.data() + qty.size(), quantity);
    if (ec != std::errc{} || end != qty.data() + qty.size() || quantity <= 0) {
        result.error = "quantity must be a positive integer";
        return result;
    }
    result.order.quantity = quantity;

    if (tokens.size() == 4) {
        result.order.price = parsePrice(tokens[3]);
        if (!result.order.price) {
            result.error = "price must be a positive decimal with at most 2 decimal places";
            return result;
        }
    }

    result.order.id = std::string(tokens[0]);
    result.status = LineResult::Status::Ok;
    return result;
}

}  // namespace ome::text
