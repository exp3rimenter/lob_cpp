#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "lob/order_book.hpp"
#include "lob/matching_engine.hpp"

using namespace lob;
using namespace testing;

static std::atomic<OrderId> g_id{1};
static OrderId next_id() { return g_id.fetch_add(1, std::memory_order_relaxed); }

class RecordingHandler final : public IEventHandler {
public:
    std::vector<Trade>           trades;
    std::vector<ExecutionReport> fills;
    std::vector<OrderId>         cancels;
    std::vector<OrderId>         accepts;
    std::vector<OrderId>         rejects;

    void on_order_accepted(const Order& o) override {
        accepts.push_back(o.id);
    }
    void on_order_rejected(const Order& o, RejectReason) override {
        rejects.push_back(o.id);
    }
    void on_order_filled(const Order& o, const Trade& t) override {
        fills.push_back({o.id, o.status, o.price,
                         o.filled_quantity, o.remaining(), t.timestamp, RejectReason::None});
    }
    void on_order_partially_filled(const Order& o, const Trade& t) override {
        fills.push_back({o.id, o.status, o.price,
                         o.filled_quantity, o.remaining(), t.timestamp, RejectReason::None});
    }
    void on_order_cancelled(const Order& o) override {
        cancels.push_back(o.id);
    }
    void on_trade(const Trade& t) override {
        trades.push_back(t);
    }
    void on_book_update(std::string_view, Side, Price, Quantity) override {}

    void reset() {
        trades.clear(); fills.clear();
        cancels.clear(); accepts.clear(); rejects.clear();
    }
};

static NewOrderRequest make_limit(OrderId id, Side side, Price px, Quantity qty,
                                  const std::string& sym = "TEST") {
    return NewOrderRequest{
        .client_order_id = id,
        .symbol          = sym,
        .side            = side,
        .type            = OrderType::Limit,
        .tif             = TimeInForce::GTC,
        .price           = px,
        .quantity        = qty,
    };
}

static NewOrderRequest make_market(OrderId id, Side side, Quantity qty,
                                   const std::string& sym = "TEST") {
    return NewOrderRequest{
        .client_order_id = id,
        .symbol          = sym,
        .side            = side,
        .type            = OrderType::Market,
        .tif             = TimeInForce::GTC,
        .price           = (side == Side::Buy)
                            ? std::numeric_limits<Price>::max()
                            : std::numeric_limits<Price>::min(),
        .quantity        = qty,
    };
}

static NewOrderRequest make_ioc(OrderId id, Side side, Price px, Quantity qty,
                                const std::string& sym = "TEST") {
    auto r = make_limit(id, side, px, qty, sym);
    r.type = OrderType::IOC;
    r.tif  = TimeInForce::IOC;
    return r;
}

static NewOrderRequest make_fok(OrderId id, Side side, Price px, Quantity qty,
                                const std::string& sym = "TEST") {
    auto r = make_limit(id, side, px, qty, sym);
    r.type = OrderType::FOK;
    r.tif  = TimeInForce::FOK;
    return r;
}

class OrderBookTest : public Test {
protected:
    RecordingHandler handler;
    OrderBook        book{"TEST", handler};
};

TEST_F(OrderBookTest, EmptyBookHasNoBestBidOrAsk) {
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.mid_price().has_value());
    EXPECT_FALSE(book.spread().has_value());
}

TEST_F(OrderBookTest, SingleBidRestingInBook) {
    auto r = book.submit(make_limit(next_id(), Side::Buy, 10000, 100));
    EXPECT_EQ(r.status, OrderStatus::New);
    EXPECT_EQ(r.remaining_qty, 100u);
    EXPECT_EQ(book.best_bid(), 10000);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST_F(OrderBookTest, SingleAskRestingInBook) {
    book.submit(make_limit(next_id(), Side::Sell, 10100, 50));
    EXPECT_EQ(book.best_ask(), 10100);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST_F(OrderBookTest, SpreadAndMidPrice) {
    book.submit(make_limit(next_id(), Side::Buy,  10000, 100));
    book.submit(make_limit(next_id(), Side::Sell, 10100,  50));
    EXPECT_EQ(book.spread(),    100);
    EXPECT_EQ(book.mid_price(), 10050);
}

TEST_F(OrderBookTest, LimitOrderCrossesAndFills) {
    auto bid_id = next_id();
    book.submit(make_limit(bid_id, Side::Buy, 10100, 100));
    EXPECT_EQ(book.order_count(), 1u);

    auto ask_id = next_id();
    auto r = book.submit(make_limit(ask_id, Side::Sell, 10000, 100));
    EXPECT_EQ(r.status, OrderStatus::Filled);
    EXPECT_EQ(r.filled_qty, 100u);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(handler.trades.size(), 1u);
    EXPECT_EQ(handler.trades[0].quantity, 100u);
    EXPECT_EQ(handler.trades[0].price, 10100);
}

TEST_F(OrderBookTest, PartialFill) {
    book.submit(make_limit(next_id(), Side::Buy, 10000, 100));
    auto r = book.submit(make_limit(next_id(), Side::Sell, 10000, 60));
    EXPECT_EQ(r.status, OrderStatus::Filled);
    EXPECT_EQ(book.bid_qty_at(10000), 40u);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(handler.trades.size(), 1u);
    EXPECT_EQ(handler.trades[0].quantity, 60u);
}

TEST_F(OrderBookTest, SweepsMultipleLevels) {
    book.submit(make_limit(next_id(), Side::Buy, 10000, 100));
    book.submit(make_limit(next_id(), Side::Buy,  9900, 200));
    book.submit(make_limit(next_id(), Side::Buy,  9800, 300));

    auto r = book.submit(make_limit(next_id(), Side::Sell, 9800, 600));
    EXPECT_EQ(r.status, OrderStatus::Filled);
    EXPECT_EQ(handler.trades.size(), 3u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(OrderBookTest, NoFillWhenSpreadDoesNotCross) {
    book.submit(make_limit(next_id(), Side::Buy,  9900, 100));
    book.submit(make_limit(next_id(), Side::Sell, 10100, 100));
    EXPECT_EQ(handler.trades.size(), 0u);
    EXPECT_EQ(book.order_count(), 2u);
}

TEST_F(OrderBookTest, MarketBuySweepsAsks) {
    book.submit(make_limit(next_id(), Side::Sell, 10000, 50));
    book.submit(make_limit(next_id(), Side::Sell, 10100, 50));
    auto r = book.submit(make_market(next_id(), Side::Buy, 80));
    EXPECT_EQ(r.filled_qty, 80u);
    EXPECT_EQ(handler.trades.size(), 2u);
}

TEST_F(OrderBookTest, MarketOrderCancelledWhenNoLiquidity) {
    auto r = book.submit(make_market(next_id(), Side::Buy, 100));
    EXPECT_EQ(r.status, OrderStatus::Cancelled);
    EXPECT_EQ(r.filled_qty, 0u);
}

TEST_F(OrderBookTest, IOCPartiallyFilledRemainingCancelled) {
    book.submit(make_limit(next_id(), Side::Sell, 10000, 50));
    auto r = book.submit(make_ioc(next_id(), Side::Buy, 10000, 200));
    EXPECT_EQ(r.filled_qty, 50u);
    EXPECT_EQ(r.status, OrderStatus::Cancelled);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(OrderBookTest, IOCNoFillCancelled) {
    book.submit(make_limit(next_id(), Side::Sell, 10500, 50));
    auto r = book.submit(make_ioc(next_id(), Side::Buy, 10000, 100));
    EXPECT_EQ(r.filled_qty, 0u);
    EXPECT_EQ(r.status, OrderStatus::Cancelled);
}

TEST_F(OrderBookTest, FOKFullyFilled) {
    book.submit(make_limit(next_id(), Side::Sell, 10000, 100));
    auto r = book.submit(make_fok(next_id(), Side::Buy, 10000, 100));
    EXPECT_EQ(r.status, OrderStatus::Filled);
    EXPECT_EQ(r.filled_qty, 100u);
}

TEST_F(OrderBookTest, FOKRollsBackWhenInsufficientLiquidity) {
    auto maker_id = next_id();
    book.submit(make_limit(maker_id, Side::Sell, 10000, 50));
    auto r = book.submit(make_fok(next_id(), Side::Buy, 10000, 100));
    EXPECT_EQ(r.status, OrderStatus::Cancelled);
    EXPECT_EQ(r.filled_qty, 100u);
    EXPECT_EQ(book.ask_qty_at(10000), 50u);
}

TEST_F(OrderBookTest, CancelRestingOrder) {
    auto id = next_id();
    book.submit(make_limit(id, Side::Buy, 10000, 100));
    EXPECT_EQ(book.order_count(), 1u);
    auto r = book.cancel(CancelOrderRequest{id, "TEST"});
    EXPECT_EQ(r.status, OrderStatus::Cancelled);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST_F(OrderBookTest, CancelNonExistentOrder) {
    auto r = book.cancel(CancelOrderRequest{9999, "TEST"});
    EXPECT_EQ(r.reject_reason, RejectReason::OrderNotFound);
}

TEST_F(OrderBookTest, ModifyOrder) {
    auto id = next_id();
    book.submit(make_limit(id, Side::Buy, 10000, 100));
    auto r = book.modify(ModifyOrderRequest{id, "TEST", 10050, 200});
    EXPECT_EQ(r.status, OrderStatus::New);
    EXPECT_EQ(book.best_bid(), 10050);
    EXPECT_EQ(book.bid_qty_at(10050), 200u);
    EXPECT_EQ(book.bid_qty_at(10000), 0u);
}

TEST_F(OrderBookTest, RejectDuplicateOrderId) {
    auto id = next_id();
    book.submit(make_limit(id, Side::Buy, 10000, 100));
    auto r = book.submit(make_limit(id, Side::Buy, 10000, 100));
    EXPECT_EQ(r.reject_reason, RejectReason::DuplicateOrderId);
}

TEST_F(OrderBookTest, RejectZeroQuantity) {
    auto r = book.submit(make_limit(next_id(), Side::Buy, 10000, 0));
    EXPECT_EQ(r.reject_reason, RejectReason::InvalidQuantity);
}

TEST_F(OrderBookTest, RejectNegativePrice) {
    auto r = book.submit(make_limit(next_id(), Side::Buy, -1, 100));
    EXPECT_EQ(r.reject_reason, RejectReason::InvalidPrice);
}

TEST_F(OrderBookTest, DepthSnapshot) {
    book.submit(make_limit(next_id(), Side::Buy,  10000, 100));
    book.submit(make_limit(next_id(), Side::Buy,   9900, 200));
    book.submit(make_limit(next_id(), Side::Sell, 10100,  50));
    book.submit(make_limit(next_id(), Side::Sell, 10200,  75));

    auto d = book.depth(5);
    ASSERT_EQ(d.bids.size(), 2u);
    ASSERT_EQ(d.asks.size(), 2u);
    EXPECT_EQ(d.bids[0].price,    10000);
    EXPECT_EQ(d.bids[0].quantity, 100u);
    EXPECT_EQ(d.asks[0].price,    10100);
    EXPECT_EQ(d.asks[0].quantity,  50u);
}

TEST_F(OrderBookTest, MultipleMakersAtSameLevel) {
    auto id1 = next_id();
    auto id2 = next_id();
    book.submit(make_limit(id1, Side::Sell, 10000, 30));
    book.submit(make_limit(id2, Side::Sell, 10000, 40));
    EXPECT_EQ(book.ask_qty_at(10000), 70u);

    book.submit(make_market(next_id(), Side::Buy, 70));
    EXPECT_EQ(book.ask_qty_at(10000), 0u);
    EXPECT_EQ(handler.trades.size(), 2u);
}

TEST_F(OrderBookTest, PricePriorityForBuys) {
    book.submit(make_limit(next_id(), Side::Buy, 10100, 100));
    book.submit(make_limit(next_id(), Side::Buy,  9900, 200));
    book.submit(make_limit(next_id(), Side::Buy, 10000, 150));

    EXPECT_EQ(book.best_bid(), 10100);
}

TEST_F(OrderBookTest, PricePriorityForSells) {
    book.submit(make_limit(next_id(), Side::Sell, 10300, 100));
    book.submit(make_limit(next_id(), Side::Sell, 10100, 200));
    book.submit(make_limit(next_id(), Side::Sell, 10200, 150));

    EXPECT_EQ(book.best_ask(), 10100);
}

TEST_F(OrderBookTest, StatsTracking) {
    book.submit(make_limit(next_id(), Side::Buy, 10000, 100));
    book.submit(make_limit(next_id(), Side::Sell, 10000, 100));
    const auto& s = book.stats();
    EXPECT_EQ(s.trades_executed, 1u);
    EXPECT_EQ(s.total_traded_quantity, 100u);
    EXPECT_GE(s.orders_filled, 1u);
}

class MatchingEngineTest : public Test {
protected:
    RecordingHandler handler;
    MatchingEngine   engine{handler};

    void SetUp() override {
        engine.add_instrument("AAPL");
        engine.add_instrument("MSFT");
    }
};

TEST_F(MatchingEngineTest, AddAndRemoveInstruments) {
    EXPECT_TRUE(engine.has_instrument("AAPL"));
    EXPECT_TRUE(engine.has_instrument("MSFT"));
    EXPECT_FALSE(engine.has_instrument("GOOG"));

    engine.add_instrument("GOOG");
    EXPECT_TRUE(engine.has_instrument("GOOG"));
    engine.remove_instrument("GOOG");
    EXPECT_FALSE(engine.has_instrument("GOOG"));
}

TEST_F(MatchingEngineTest, OrdersRoutedToCorrectBook) {
    engine.submit(make_limit(next_id(), Side::Buy, 17000, 100, "AAPL"));
    engine.submit(make_limit(next_id(), Side::Buy, 38000, 200, "MSFT"));

    EXPECT_EQ(engine.book("AAPL")->best_bid(), 17000);
    EXPECT_EQ(engine.book("MSFT")->best_bid(), 38000);
}

TEST_F(MatchingEngineTest, RejectsUnknownSymbol) {
    auto r = engine.submit(make_limit(next_id(), Side::Buy, 10000, 100, "GOOG"));
    EXPECT_EQ(r.reject_reason, RejectReason::InvalidSymbol);
}

TEST_F(MatchingEngineTest, CrossBookMatchDoesNotOccur) {
    engine.submit(make_limit(next_id(), Side::Buy,  10000, 100, "AAPL"));
    engine.submit(make_limit(next_id(), Side::Sell,  9000, 100, "MSFT"));
    EXPECT_EQ(handler.trades.size(), 0u);
}

int main(int argc, char** argv) {
    InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
