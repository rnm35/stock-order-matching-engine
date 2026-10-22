// LOBSTER adapter. The message lines here are hand-made in the documented format, not
// taken from LOBSTER data.

#include "ome/lobster.hpp"

#include <gtest/gtest.h>

using namespace ome;
using namespace ome::lobster;

namespace {

Message msg(EventType type, std::uint64_t id, Quantity size, std::int64_t price, Side side) {
    Message m;
    m.type = type;
    m.orderId = id;
    m.size = size;
    m.price = price;
    m.side = side;
    return m;
}

TEST(LobsterParse, ReadsAllSixFields) {
    const auto m = parseMessage("34200.004241176,1,16113575,18,5853300,-1\r");
    ASSERT_TRUE(m.has_value());
    EXPECT_DOUBLE_EQ(m->time, 34200.004241176);
    EXPECT_EQ(m->type, EventType::Submit);
    EXPECT_EQ(m->orderId, 16113575u);
    EXPECT_EQ(m->size, 18);
    EXPECT_EQ(m->price, 5853300);
    EXPECT_EQ(m->side, Side::Sell);
}

TEST(LobsterParse, IgnoresExtraColumns) {
    EXPECT_TRUE(parseMessage("34200.1,3,7,100,5853300,1,extra,columns").has_value());
}

TEST(LobsterParse, RejectsMalformedLines) {
    for (const char* line :
         {"", "34200.1,1,7,100,5853300", "34200.1,8,7,100,5853300,1", "34200.1,1,7,100,5853300,0",
          "x,1,7,100,5853300,1", "34200.1,1,seven,100,5853300,1", "34200.1,1,7,100,5853300,"}) {
        EXPECT_FALSE(parseMessage(line).has_value()) << line;
    }
}

TEST(LobsterReplay, ExecutionsHitTheSameRestingOrder) {
    Replayer r;
    r.apply(msg(EventType::Submit, 1, 100, 911'400, Side::Sell));  // $91.14
    r.apply(msg(EventType::Submit, 2, 50, 911'400, Side::Sell));
    r.apply(msg(EventType::ExecuteVisible, 1, 30, 911'400, Side::Sell));

    EXPECT_EQ(r.stats().executionsMatched, 1u);
    EXPECT_EQ(r.stats().executionsMismatched, 0u);
    EXPECT_EQ(r.book().find(1).value().remaining, 70);
    EXPECT_EQ(r.book().find(2).value().remaining, 50);
}

TEST(LobsterReplay, PartialCancelKeepsQueuePosition) {
    Replayer r;
    r.apply(msg(EventType::Submit, 1, 100, 911'400, Side::Buy));
    r.apply(msg(EventType::Submit, 2, 100, 911'400, Side::Buy));
    r.apply(msg(EventType::PartialCancel, 1, 60, 911'400, Side::Buy));
    // Order 1 is still first, so this execution must hit it.
    r.apply(msg(EventType::ExecuteVisible, 1, 40, 911'400, Side::Buy));

    EXPECT_EQ(r.stats().executionsMatched, 1u);
    EXPECT_FALSE(r.book().contains(1));
}

TEST(LobsterReplay, DetectsWhenOurBookWouldFillADifferentOrder) {
    Replayer r;
    r.apply(msg(EventType::Submit, 1, 100, 911'400, Side::Sell));
    r.apply(msg(EventType::Submit, 2, 100, 911'400, Side::Sell));
    // Claims order 2 traded first; our FIFO book fills order 1.
    r.apply(msg(EventType::ExecuteVisible, 2, 10, 911'400, Side::Sell));

    EXPECT_EQ(r.stats().executionsMismatched, 1u);
    EXPECT_EQ(r.book().find(1).value().remaining, 90);
}

TEST(LobsterReplay, CountsWhatItCannotApply) {
    Replayer r;
    r.apply(msg(EventType::Delete, 99, 10, 911'400, Side::Buy));          // never seen
    r.apply(msg(EventType::ExecuteVisible, 98, 10, 911'400, Side::Buy));  // never seen
    r.apply(msg(EventType::Submit, 3, 10, 911'450, Side::Buy));           // half a cent
    r.apply(msg(EventType::ExecuteHidden, 0, 10, 911'400, Side::Buy));
    r.apply(msg(EventType::Halt, 0, 0, -1, Side::Buy));

    const auto& s = r.stats();
    EXPECT_EQ(s.messages, 5u);
    EXPECT_EQ(s.unknownOrder, 2u);
    EXPECT_EQ(s.subTickPrice, 1u);
    EXPECT_EQ(s.ignored, 2u);
    EXPECT_EQ(r.book().orderCount(), 0u);
}

TEST(LobsterOrderbook, ReadsTopOfBookAndEmptySides) {
    const auto row = parseOrderbookRow("5859400,200,5853300,18,5859500,100,5853200,50\r");
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->askPrice, 5859400);
    EXPECT_EQ(row->askSize, 200);
    EXPECT_EQ(row->bidPrice, 5853300);
    EXPECT_EQ(row->bidSize, 18);

    // LOBSTER's dummy entry for an empty side: extreme price, size 0.
    const auto empty = parseOrderbookRow("9999999999,0,5853300,18");
    ASSERT_TRUE(empty.has_value());
    EXPECT_FALSE(empty->askPrice.has_value());

    EXPECT_FALSE(parseOrderbookRow("5859400,200,5853300").has_value());
}

TopOfBook top(std::int64_t ask, Quantity askSize, std::int64_t bid, Quantity bidSize) {
    TopOfBook t;
    t.askPrice = ask;
    t.askSize = askSize;
    t.bidPrice = bid;
    t.bidSize = bidSize;
    return t;
}

TEST(LobsterReconcile, RemovesOrdersBetterThanTheTrueBest) {
    Replayer r;
    r.apply(
        msg(EventType::Submit, 1, 50, 911'400, Side::Buy));  // stale: its delete was filtered out
    r.apply(msg(EventType::Submit, 2, 20, 911'300, Side::Buy));
    r.apply(msg(EventType::Submit, 3, 10, 911'600, Side::Sell));
    // The real book's best bid is 91.13, so order 1 at 91.14 can't exist any more.
    r.reconcile(top(911'600, 10, 911'300, 20));

    EXPECT_EQ(r.stats().topOfBookChecked, 1u);
    EXPECT_EQ(r.stats().topOfBookMatched, 0u);  // compared before cleaning up
    EXPECT_EQ(r.stats().staleOrdersRemoved, 1u);
    EXPECT_FALSE(r.book().contains(1));
    EXPECT_EQ(r.book().bestBid(), 9'113);

    r.reconcile(top(911'600, 10, 911'300, 20));
    EXPECT_EQ(r.stats().topOfBookMatched, 1u);
}

TEST(LobsterReconcile, AnEmptyTrueSideClearsOurs) {
    Replayer r;
    r.apply(msg(EventType::Submit, 1, 50, 911'600, Side::Sell));
    TopOfBook t = top(0, 0, 911'300, 20);
    t.askPrice.reset();
    r.reconcile(t);
    EXPECT_FALSE(r.book().contains(1));
}

TEST(LobsterReplay, DeleteRemovesTheOrder) {
    Replayer r;
    r.apply(msg(EventType::Submit, 1, 100, 911'400, Side::Buy));
    r.apply(msg(EventType::Delete, 1, 100, 911'400, Side::Buy));
    EXPECT_EQ(r.book().orderCount(), 0u);
    EXPECT_EQ(r.stats().unknownOrder, 0u);
}

}  // namespace
