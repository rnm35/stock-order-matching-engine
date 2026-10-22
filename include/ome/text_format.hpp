#pragma once

#include "ome/types.hpp"

#include <optional>
#include <string>
#include <string_view>

// The original program's text formats. Decimal prices are converted to ticks here, at the
// edge, and nowhere else.
namespace ome::text {

// Parses a positive decimal price with at most two significant decimal places, e.g.
// "100", "100.5", "100.25", ".5". Extra trailing zeros ("100.500") are accepted because
// they are exact; anything that would need rounding ("100.123") is rejected.
std::optional<Price> parsePrice(std::string_view text);

// Formats ticks with exactly two decimal places, e.g. 10050 -> "100.50".
std::string formatPrice(Price ticks);

// One order line: "<order_id> <B|S> <quantity> [price]". No price means a market order.
struct OrderLine {
    std::string id;
    Side side = Side::Buy;
    Quantity quantity = 0;
    std::optional<Price> price;
};

struct LineResult {
    enum class Status : std::uint8_t { Ok, Blank, Error };
    Status status = Status::Blank;
    OrderLine order;         // valid when status == Ok
    std::string_view error;  // static message, valid when status == Error
};

LineResult parseOrderLine(std::string_view line);

}  // namespace ome::text
