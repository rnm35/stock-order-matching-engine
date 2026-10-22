// Modern (exchange-style) matching rules. Prices are in ticks: 10'000 == 100.00.

#include "ome/order_book.hpp"
#include "recording_listener.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace ome;

namespace {

class ModernBook : public ::testing::Test {
protected:
    RecordingListener events;
    OrderBook book{{MatchingMode::Modern, std::nullopt}, events};

    void buy(OrderId id, Quantity qty, Price px, TimeInForce tif = TimeInForce::GTC) {
        book.submit(NewOrder::limit(id, Side::Buy, qty, px, tif));
    }
    void sell(OrderId id, Quantity qty, Price px, TimeInForce tif = TimeInForce::GTC) {
        book.submit(NewOrder::limit(id, Side::Sell, qty, px, tif));
    }
    void marketBuy(OrderId id, Quantity qty, TimeInForce tif = TimeInForce::GTC) {
        book.submit(NewOrder::market(id, Side::Buy, qty, tif));
    }
    void marketSell(OrderId id, Quantity qty, TimeInForce tif = TimeInForce::GTC) {
        book.submit(NewOrder::market(id, Side::Sell, qty, tif));
    }
    std::vector<OrderId> idsOn(Side side) const {
        std::vector<OrderId> ids;
        for (const auto& o : book.orders(side)) ids.push_back(o.id);
        return ids;
    }
};

TEST_F(ModernBook, LimitOrderRestsOnEmptyBook) {
    buy(1, 10, 10'000);
    sell(2, 5, 10'100);

    EXPECT_TRUE(events.trades.empty());
    EXPECT_EQ(book.bestBid(), 10'000);
    EXPECT_EQ(book.bestAsk(), 10'100);
    EXPECT_EQ(book.orderCount(), 2u);
    ASSERT_EQ(events.accepted.size(), 2u);
    EXPECT_EQ(events.accepted[0].sequence, 1u);
    EXPECT_EQ(events.accepted[1].sequence, 2u);
}

TEST_F(ModernBook, CrossingLimitTradesAtRestingPrice) {
    sell(1, 10, 10'000);
    buy(2, 10, 10'200);  // willing to pay more, but gets the resting price

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0], (Trade{2, 1, Side::Buy, 10'000, 10}));
    EXPECT_EQ(book.orderCount(), 0u);
    EXPECT_EQ(book.lastTradePrice(), 10'000);
}

TEST_F(ModernBook, IncomingRemainderRestsAtItsLimit) {
    sell(1, 4, 10'000);
    buy(2, 10, 10'000);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].quantity, 4);
    ASSERT_TRUE(book.find(2));
    EXPECT_EQ(book.find(2).value().remaining, 6);
    EXPECT_EQ(book.bestBid(), 10'000);
    EXPECT_FALSE(book.bestAsk());
}

// Bug 1 of the original (README, "Fixed from the original"): ties were broken by comparing
// ids as strings.
TEST_F(ModernBook, SamePriceMatchesInArrivalOrderNotIdOrder) {
    sell(10, 5, 10'000);  // arrives first, larger id
    sell(2, 5, 10'000);
    buy(3, 5, 10'000);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].sellOrderId, 10u);
}

// Bug 1 again: the original re-appended partially filled orders to the back.
TEST_F(ModernBook, PartiallyFilledRestingOrderKeepsPriority) {
    sell(1, 10, 10'000);
    sell(2, 10, 10'000);
    buy(3, 4, 10'000);
    buy(4, 4, 10'000);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[0].sellOrderId, 1u);
    EXPECT_EQ(events.trades[1].sellOrderId, 1u);
    EXPECT_EQ(book.find(1).value().remaining, 2);
    EXPECT_EQ(idsOn(Side::Sell), (std::vector<OrderId>{1, 2}));
}

TEST_F(ModernBook, SweepsLevelsBestPriceFirst) {
    sell(1, 5, 10'200);
    sell(2, 5, 10'000);
    sell(3, 5, 10'100);
    buy(4, 12, 10'200);

    ASSERT_EQ(events.trades.size(), 3u);
    EXPECT_EQ(events.trades[0], (Trade{4, 2, Side::Buy, 10'000, 5}));
    EXPECT_EQ(events.trades[1], (Trade{4, 3, Side::Buy, 10'100, 5}));
    EXPECT_EQ(events.trades[2], (Trade{4, 1, Side::Buy, 10'200, 2}));
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'200), 3);
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'000), 0);
}

TEST_F(ModernBook, SellSweepsBidsHighestFirst) {
    buy(1, 5, 9'900);
    buy(2, 5, 10'000);
    sell(3, 7, 9'900);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[0], (Trade{2, 3, Side::Sell, 10'000, 5}));
    EXPECT_EQ(events.trades[1], (Trade{1, 3, Side::Sell, 9'900, 2}));
}

TEST_F(ModernBook, LimitNeverTradesThroughItsPrice) {
    sell(1, 5, 10'000);
    sell(2, 5, 10'100);
    buy(3, 10, 10'050);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].sellOrderId, 1u);
    EXPECT_EQ(book.bestBid(), 10'050);
    EXPECT_EQ(book.bestAsk(), 10'100);
}

TEST_F(ModernBook, MarketOrderRemainderIsCancelledNotRested) {
    sell(1, 5, 10'000);
    marketBuy(2, 8);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0], (Trade{2, 1, Side::Buy, 10'000, 5}));
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].id, 2u);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 3);
    EXPECT_EQ(events.cancelled[0].reason, CancelReason::ImmediateRemainder);
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_F(ModernBook, MarketOrderOnEmptyBookIsCancelled) {
    marketSell(1, 10);

    EXPECT_TRUE(events.trades.empty());
    ASSERT_EQ(events.accepted.size(), 1u);
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 10);
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_F(ModernBook, IocFillsWhatItCanAndCancelsTheRest) {
    sell(1, 3, 10'000);
    buy(2, 10, 10'000, TimeInForce::IOC);

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].quantity, 3);
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 7);
    EXPECT_EQ(events.cancelled[0].reason, CancelReason::ImmediateRemainder);
    EXPECT_FALSE(book.contains(2));
}

TEST_F(ModernBook, FokThatCannotFillCompletelyDoesNotTradeAtAll) {
    sell(1, 5, 10'000);
    sell(2, 5, 10'200);  // beyond the FOK's limit
    buy(3, 8, 10'100, TimeInForce::FOK);

    EXPECT_TRUE(events.trades.empty());
    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].reason, CancelReason::FillOrKill);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 8);
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'000), 5);  // untouched
}

TEST_F(ModernBook, FokFillsAcrossSeveralLevels) {
    sell(1, 5, 10'000);
    sell(2, 5, 10'100);
    buy(3, 8, 10'100, TimeInForce::FOK);

    ASSERT_EQ(events.trades.size(), 2u);
    EXPECT_EQ(events.trades[0].quantity, 5);
    EXPECT_EQ(events.trades[1].quantity, 3);
    EXPECT_TRUE(events.cancelled.empty());
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'100), 2);
}

TEST_F(ModernBook, CancelRemovesOrderAndEmptiedLevel) {
    buy(1, 10, 10'000);
    buy(2, 10, 9'900);
    book.cancel(1);

    ASSERT_EQ(events.cancelled.size(), 1u);
    EXPECT_EQ(events.cancelled[0].cancelledQuantity, 10);
    EXPECT_EQ(events.cancelled[0].reason, CancelReason::UserRequest);
    EXPECT_FALSE(book.contains(1));
    EXPECT_EQ(book.bestBid(), 9'900);
    EXPECT_TRUE(book.depth(Side::Buy).size() == 1);
}

TEST_F(ModernBook, CancelFromMiddleOfQueueKeepsOthersInOrder) {
    sell(1, 1, 10'000);
    sell(2, 2, 10'000);
    sell(3, 3, 10'000);
    book.cancel(2);

    EXPECT_EQ(idsOn(Side::Sell), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'000), 4);
}

TEST_F(ModernBook, CancelUnknownOrderIsRejected) {
    book.cancel(42);
    ASSERT_EQ(events.rejected.size(), 1u);
    EXPECT_EQ(events.rejected[0].request, RequestType::Cancel);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::UnknownOrder);
}

TEST_F(ModernBook, FilledOrderCanNoLongerBeCancelled) {
    sell(1, 5, 10'000);
    buy(2, 5, 10'000);
    book.cancel(1);
    ASSERT_EQ(events.rejected.size(), 1u);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::UnknownOrder);
}

TEST_F(ModernBook, ReducingQuantityKeepsPriority) {
    sell(1, 10, 10'000);
    sell(2, 10, 10'000);
    book.modify(1, 10'000, 4);

    ASSERT_EQ(events.modified.size(), 1u);
    EXPECT_TRUE(events.modified[0].priorityKept);
    EXPECT_EQ(idsOn(Side::Sell), (std::vector<OrderId>{1, 2}));
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'000), 14);

    buy(3, 4, 10'000);
    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0].sellOrderId, 1u);
}

TEST_F(ModernBook, IncreasingQuantityLosesPriority) {
    sell(1, 10, 10'000);
    sell(2, 10, 10'000);
    book.modify(1, 10'000, 15);

    ASSERT_EQ(events.modified.size(), 1u);
    EXPECT_FALSE(events.modified[0].priorityKept);
    EXPECT_GT(events.modified[0].sequence, book.find(2).value().sequence);
    EXPECT_EQ(idsOn(Side::Sell), (std::vector<OrderId>{2, 1}));
    EXPECT_EQ(book.volumeAt(Side::Sell, 10'000), 25);
}

TEST_F(ModernBook, PriceChangeMovesLevelAndCanTrade) {
    buy(1, 10, 9'900);
    sell(2, 4, 10'000);
    book.modify(1, 10'000, 10);  // now crosses the ask

    ASSERT_EQ(events.trades.size(), 1u);
    EXPECT_EQ(events.trades[0], (Trade{1, 2, Side::Buy, 10'000, 4}));
    EXPECT_EQ(book.volumeAt(Side::Buy, 9'900), 0);
    EXPECT_EQ(book.volumeAt(Side::Buy, 10'000), 6);
}

TEST_F(ModernBook, InvalidModifiesAreRejected) {
    buy(1, 10, 10'000);
    book.modify(1, 10'000, 0);
    book.modify(1, 0, 5);
    book.modify(99, 10'000, 5);

    ASSERT_EQ(events.rejected.size(), 3u);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::InvalidQuantity);
    EXPECT_EQ(events.rejected[1].reason, RejectReason::InvalidPrice);
    EXPECT_EQ(events.rejected[2].reason, RejectReason::UnknownOrder);
    EXPECT_EQ(book.find(1).value().remaining, 10);
}

TEST_F(ModernBook, DuplicateRestingIdIsRejected) {
    buy(1, 10, 10'000);
    sell(1, 5, 10'500);

    ASSERT_EQ(events.rejected.size(), 1u);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::DuplicateOrderId);
    EXPECT_FALSE(book.bestAsk());
}

TEST_F(ModernBook, InvalidNewOrdersAreRejected) {
    buy(1, 0, 10'000);
    buy(2, -5, 10'000);
    sell(3, 5, 0);

    ASSERT_EQ(events.rejected.size(), 3u);
    EXPECT_EQ(events.rejected[0].reason, RejectReason::InvalidQuantity);
    EXPECT_EQ(events.rejected[1].reason, RejectReason::InvalidQuantity);
    EXPECT_EQ(events.rejected[2].reason, RejectReason::InvalidPrice);
    EXPECT_TRUE(events.accepted.empty());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_F(ModernBook, OrdersAtListsOneLevelInQueueOrder) {
    sell(5, 1, 10'000);
    sell(3, 2, 10'000);
    sell(4, 3, 10'100);

    const auto level = book.ordersAt(Side::Sell, 10'000);
    ASSERT_EQ(level.size(), 2u);
    EXPECT_EQ(level[0].id, 5u);
    EXPECT_EQ(level[1].id, 3u);
    EXPECT_TRUE(book.ordersAt(Side::Sell, 9'900).empty());
    EXPECT_TRUE(book.ordersAt(Side::Buy, 10'000).empty());
}

TEST_F(ModernBook, DepthReportsLevelsBestFirst) {
    buy(1, 10, 10'000);
    buy(2, 5, 10'000);
    buy(3, 7, 9'900);
    buy(4, 1, 9'800);

    const auto depth = book.depth(Side::Buy, 2);
    ASSERT_EQ(depth.size(), 2u);
    EXPECT_EQ(depth[0].price, 10'000);
    EXPECT_EQ(depth[0].quantity, 15);
    EXPECT_EQ(depth[0].orderCount, 2u);
    EXPECT_EQ(depth[1].price, 9'900);
    EXPECT_EQ(depth[1].quantity, 7);
}

}  // namespace
