#include "ome/text_format.hpp"

#include <gtest/gtest.h>

using namespace ome;
using namespace ome::text;
using Status = LineResult::Status;

namespace {

TEST(ParsePrice, AcceptsDecimalsUpToOneTick) {
    EXPECT_EQ(parsePrice("100"), 10'000);
    EXPECT_EQ(parsePrice("100.5"), 10'050);
    EXPECT_EQ(parsePrice("100.25"), 10'025);
    EXPECT_EQ(parsePrice("100."), 10'000);
    EXPECT_EQ(parsePrice(".5"), 50);
    EXPECT_EQ(parsePrice("0.01"), 1);
    EXPECT_EQ(parsePrice("100.500"), 10'050);  // exact, so allowed
}

TEST(ParsePrice, RejectsAnythingInexactOrNotPositive) {
    EXPECT_FALSE(parsePrice(""));
    EXPECT_FALSE(parsePrice("."));
    EXPECT_FALSE(parsePrice("0"));
    EXPECT_FALSE(parsePrice("0.00"));
    EXPECT_FALSE(parsePrice("-1.00"));
    EXPECT_FALSE(parsePrice("+1.00"));
    EXPECT_FALSE(parsePrice("100.123"));
    EXPECT_FALSE(parsePrice("1e2"));
    EXPECT_FALSE(parsePrice("100.00abc"));
    EXPECT_FALSE(parsePrice("1,000.00"));
    EXPECT_FALSE(parsePrice("1234567890123456"));  // too many digits for int64 ticks
}

TEST(FormatPrice, AlwaysTwoDecimals) {
    EXPECT_EQ(formatPrice(10'050), "100.50");
    EXPECT_EQ(formatPrice(10'000), "100.00");
    EXPECT_EQ(formatPrice(1), "0.01");
    EXPECT_EQ(formatPrice(0), "0.00");
    EXPECT_EQ(formatPrice(-150), "-1.50");
}

TEST(ParsePrice, RoundTripsThroughFormat) {
    for (Price p : {Price{1}, Price{99}, Price{100}, Price{10'025}, Price{123'456'789}}) {
        EXPECT_EQ(parsePrice(formatPrice(p)), p);
    }
}

TEST(ParseOrderLine, LimitOrder) {
    const auto r = parseOrderLine("o1 B 100 101.25");
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.order.id, "o1");
    EXPECT_EQ(r.order.side, Side::Buy);
    EXPECT_EQ(r.order.quantity, 100);
    EXPECT_EQ(r.order.price, 10'125);
}

TEST(ParseOrderLine, MissingPriceMeansMarketOrder) {
    const auto r = parseOrderLine("o3 S 100");
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.order.side, Side::Sell);
    EXPECT_FALSE(r.order.price);
}

TEST(ParseOrderLine, ToleratesTabsAndWindowsLineEndings) {
    const auto r = parseOrderLine("  o1\tS  7 99.5\r");
    ASSERT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.order.quantity, 7);
    EXPECT_EQ(r.order.price, 9'950);
}

TEST(ParseOrderLine, BlankLines) {
    EXPECT_EQ(parseOrderLine("").status, Status::Blank);
    EXPECT_EQ(parseOrderLine(" \t\r").status, Status::Blank);
}

TEST(ParseOrderLine, RejectsMalformedLines) {
    for (const char* line :
         {"o1", "o1 B", "o1 X 10 100", "o1 b 10 100", "o1 Buy 10 100", "o1 B 0 100", "o1 B -5 100",
          "o1 B 10.5 100", "o1 B ten 100", "o1 B 10 abc", "o1 B 10 0", "o1 B 10 100.001",
          "o1 B 10 100 x", "o1 B 99999999999999999999 100"}) {
        const auto r = parseOrderLine(line);
        EXPECT_EQ(r.status, Status::Error) << line;
        EXPECT_FALSE(r.error.empty()) << line;
    }
}

}  // namespace
