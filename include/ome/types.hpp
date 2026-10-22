#pragma once

#include <cstdint>
#include <string_view>

namespace ome {

// Prices are integer ticks so that equality and ordering are exact. Doubles would make
// "same price" comparisons (and therefore FIFO priority) depend on rounding.
using Price = std::int64_t;
inline constexpr Price kTicksPerUnit = 100;  // 1 tick = 0.01

// Signed so that an accidental over-subtraction shows up as a negative number
// instead of silently wrapping to a huge value.
using Quantity = std::int64_t;

using OrderId = std::uint64_t;

// Assigned by the book when an order is accepted (or loses priority on modify).
// Time priority is defined by this number, never by OrderId.
using Sequence = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };

// GTC: rest any unfilled remainder (Good-Til-Cancelled).
// IOC: fill what is possible now, cancel the rest (Immediate-Or-Cancel).
// FOK: fill the whole quantity now or nothing at all (Fill-Or-Kill).
enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

// Modern: exchange-style. Market orders never rest; every trade prints at the resting
//         order's price.
// Legacy: the original coursework rules (see README "Matching Behavior"): market orders
//         rest and take priority over limit orders on their side; market-vs-market trades
//         at the last traded price; incoming limit vs resting market trades at the
//         incoming limit price.
enum class MatchingMode : std::uint8_t { Legacy, Modern };

constexpr Side opposite(Side s) noexcept { return s == Side::Buy ? Side::Sell : Side::Buy; }

std::string_view toString(Side s) noexcept;
std::string_view toString(OrderType t) noexcept;
std::string_view toString(TimeInForce tif) noexcept;
std::string_view toString(MatchingMode m) noexcept;

}  // namespace ome
