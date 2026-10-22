// The differential harness itself, plus a quick random run so a mismatch also shows up as a
// named GoogleTest failure. The long runs live in ome-difftest (see tests/CMakeLists.txt).

#include "differential.hpp"
#include "recording_listener.hpp"
#include "reference_book.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace ome;
using namespace ome::test;

namespace {

// The oracle has to be right on its own, independently of OrderBook.
TEST(ReferenceBook, ReproducesTheReadmeExample) {
    RecordingListener events;
    ReferenceBook ref({MatchingMode::Legacy, Price{10'050}}, events);
    ref.submit(NewOrder::limit(1, Side::Buy, 100, 10'125));
    ref.submit(NewOrder::limit(2, Side::Sell, 50, 10'075));
    ref.submit(NewOrder::market(3, Side::Sell, 100));
    ref.submit(NewOrder::market(4, Side::Buy, 40));

    ASSERT_EQ(events.trades.size(), 3u);
    EXPECT_EQ(events.trades[0], (Trade{1, 2, Side::Sell, 10'125, 50}));
    EXPECT_EQ(events.trades[1], (Trade{1, 3, Side::Sell, 10'125, 50}));
    EXPECT_EQ(events.trades[2], (Trade{4, 3, Side::Buy, 10'125, 40}));  // market vs market
    const auto sells = ref.orders(Side::Sell);
    ASSERT_EQ(sells.size(), 1u);
    EXPECT_EQ(sells[0].remaining, 10);
}

TEST(ReferenceBook, OrdersAreReturnedInPriorityOrder) {
    RecordingListener events;
    ReferenceBook ref({MatchingMode::Legacy, Price{10'000}}, events);
    ref.submit(NewOrder::limit(1, Side::Buy, 1, 9'900));
    ref.submit(NewOrder::limit(2, Side::Buy, 1, 10'000));
    ref.submit(NewOrder::market(3, Side::Buy, 1));
    ref.submit(NewOrder::limit(4, Side::Buy, 1, 10'000));

    std::vector<OrderId> ids;
    for (const auto& o : ref.orders(Side::Buy)) ids.push_back(o.id);
    EXPECT_EQ(ids, (std::vector<OrderId>{3, 2, 4, 1}));
}

TEST(Differential, AgreeingBooksReportNoDifference) {
    // Same op twice: the second submit of id 1 is a duplicate in both books, so they agree.
    DifferentialRunner runner({MatchingMode::Modern, std::nullopt});
    Op op;
    op.order = NewOrder::limit(1, Side::Buy, 5, 10'000);
    EXPECT_EQ(runner.apply(op), "");
    EXPECT_EQ(runner.apply(op), "");
    EXPECT_EQ(runner.checkState(), "");
}

TEST(Differential, DecodesFuzzBytesIntoOperations) {
    const std::array<std::uint8_t, 13> bytes{
        0x00, 0x00, 10, 1,  // limit buy #1 qty 10 @ 100.00
        0x05, 0x00, 0,  1,  // cancel #1
        0x06, 0x02, 5,  3,  // modify #3 -> 100.02 x 5
        0xFF,               // trailing partial op is ignored
    };
    const auto ops = decodeOps(bytes.data(), bytes.size());
    ASSERT_EQ(ops.size(), 3u);
    EXPECT_EQ(ops[0].kind, Op::Kind::Submit);
    EXPECT_EQ(ops[0].order.price, 10'000);
    EXPECT_EQ(ops[0].order.quantity, 10);
    EXPECT_EQ(ops[1].kind, Op::Kind::Cancel);
    EXPECT_EQ(ops[2].kind, Op::Kind::Modify);
    EXPECT_EQ(ops[2].price, 10'002);
}

class RandomStreams : public ::testing::TestWithParam<MatchingMode> {};

TEST_P(RandomStreams, BookAgreesWithReference) {
    for (std::uint64_t seed = 100; seed < 105; ++seed) {
        DifferentialRunner runner({GetParam(), Price{10'000}});
        OpGenerator gen(seed, runner.book());
        for (int i = 0; i < 5'000; ++i) {
            const Op op = gen.next();
            ASSERT_EQ(runner.apply(op), "")
                << "seed " << seed << " op " << i << ": " << op.describe();
            if (i % 50 == 0) {
                ASSERT_EQ(runner.checkState(), "") << "seed " << seed << " op " << i;
            }
        }
        ASSERT_EQ(runner.checkState(), "") << "seed " << seed;
    }
}

INSTANTIATE_TEST_SUITE_P(BothModes, RandomStreams,
                         ::testing::Values(MatchingMode::Legacy, MatchingMode::Modern),
                         [](const auto& p) { return std::string(toString(p.param)); });

}  // namespace
