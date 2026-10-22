// Legacy mode: the original coursework rules (README "Matching Behavior"), bugs fixed.

#include "ome/order_book.hpp"
#include "recording_listener.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace ome;

namespace {

class LegacyBook : public ::testing::Test {
protected:
    RecordingListener events;
    OrderBook book{{MatchingMode::Legacy, Price{10'050}}, events};  // last traded 100.50

    void limit(OrderId id, Side side, Quantity qty, Price px) {
        book.submit(NewOrder::limit(id, side, qty, px));
    }
    void market(OrderId id, Side side, Quantity qty) {
        book.submit(NewOrder::market(id, side, qty));
    }
};

TEST(LegacyConfig, RequiresAnInitialLastTradedPrice) {
    RecordingListener events;
    EXPECT_THROW(OrderBook({MatchingMode::Legacy, std::nullopt}, events), std::invalid_argument);
    EXPECT_THROW(OrderBook({MatchingMode::Legacy, Price{0}}, events), std::invalid_argument);
}

TEST_F(LegacyBook, UnfilledMarketOrderRests) {
    market(1, Side::Buy, 10);

    EXPECT_TRUE(events.cancelled.empty());
    ASSERT_TRUE(book.find(1));
    EXPECT_EQ(book.find(1).value().type, OrderType::Market);
    EXPECT_EQ(book.orders(Side::Buy).size(), 1u);
    EXPECT_FALSE(book.bestBid());  // market orders are not a price level
}

TEST_F(LegacyBook, MarketVsMarketTradesAtLastTradedPrice) {
    market(1, Side::Sell, 10);
    market(2, Side::Buy, 4);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0], (Trade{2, 1, Side::Buy, 10'050, 4}));
}

TEST_F(LegacyBook, MarketVsMarketFollowsTheLatestTrade) {
    limit(1, Side::Sell, 5, 10'200);
    market(2, Side::Buy, 5);   // trades at 102.00 -> new last traded price
    market(3, Side::Sell, 5);  // nothing to hit, rests
    market(4, Side::Buy, 5);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[1], (Trade{4, 3, Side::Buy, 10'200, 5}));
}

TEST_F(LegacyBook, IncomingMarketVsRestingLimitTradesAtLimitPrice) {
    limit(1, Side::Buy, 10, 9'900);
    market(2, Side::Sell, 10);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].price, 9'900);
}

TEST_F(LegacyBook, IncomingLimitVsRestingMarketTradesAtIncomingLimitPrice) {
    market(1, Side::Buy, 10);
    limit(2, Side::Sell, 10, 9'800);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0], (Trade{1, 2, Side::Sell, 9'800, 10}));
}

TEST_F(LegacyBook, LimitVsLimitTradesAtEarlierOrdersPrice) {
    limit(1, Side::Sell, 10, 10'000);
    limit(2, Side::Buy, 10, 10'300);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].price, 10'000);
}

TEST_F(LegacyBook, RestingMarketOrdersMatchBeforeAnyLimitOnTheirSide) {
    limit(1, Side::Buy, 5, 9'900);
    market(2, Side::Buy, 5);  // no asks to hit, so it rests, ranked ahead of order 1
    limit(3, Side::Sell, 8, 9'900);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[0], (Trade{2, 3, Side::Sell, 9'900, 5}));
    EXPECT_EQ(events.trades[1], (Trade{1, 3, Side::Sell, 9'900, 3}));
}

TEST_F(LegacyBook, RestingMarketOrdersAreFifoAmongThemselves) {
    market(10, Side::Sell, 5);
    market(2, Side::Sell, 5);
    limit(3, Side::Buy, 5, 10'000);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].sellOrderId, 10u);
}

TEST_F(LegacyBook, PartiallyFilledRestingOrderKeepsPriority) {
    limit(1, Side::Sell, 10, 10'000);
    limit(2, Side::Sell, 10, 10'000);
    market(3, Side::Buy, 4);
    market(4, Side::Buy, 4);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[1].sellOrderId, 1u);
}

TEST_F(LegacyBook, RestingMarketOrderCanBeCancelledButNotModified) {
    market(1, Side::Sell, 5);
    book.modify(1, 10'000, 5);
    ASSERT_EQ(events.rejected.size(), 1u);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::MarketOrderNotModifiable);

    book.cancel(1);
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 5);
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_F(LegacyBook, MarketIocIsCancelledInsteadOfResting) {
    book.submit(NewOrder::market(1, Side::Buy, 5, TimeInForce::IOC));
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_F(LegacyBook, FokCountsRestingMarketLiquidity) {
    market(1, Side::Sell, 5);
    limit(2, Side::Sell, 5, 10'000);
    book.submit(NewOrder::limit(3, Side::Buy, 10, 10'000, TimeInForce::FOK));

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_TRUE(events.cancelled.empty());
}

}  // namespace
