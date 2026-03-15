#pragma once

#include "types.hpp"
#include "order.hpp"
#include "price_level.hpp"
#include "pool_allocator.hpp"
#include "event_handler.hpp"

#include <map>
#include <unordered_map>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <atomic>
#include <functional>

namespace lob {

static constexpr std::size_t kMaxOrders     = 1 << 20;
static constexpr std::size_t kMaxOrderNodes = kMaxOrders;

struct BookStats {
    std::uint64_t orders_added{0};
    std::uint64_t orders_cancelled{0};
    std::uint64_t orders_filled{0};
    std::uint64_t orders_partially_filled{0};
    std::uint64_t orders_rejected{0};
    std::uint64_t trades_executed{0};
    std::uint64_t total_traded_quantity{0};
    std::uint64_t total_traded_notional{0};
};

struct Level2Entry {
    Price    price;
    Quantity quantity;
    std::size_t order_count;
};

struct MarketDepth {
    std::vector<Level2Entry> bids;
    std::vector<Level2Entry> asks;
    std::optional<Price>     best_bid;
    std::optional<Price>     best_ask;
    std::optional<Price>     mid_price;
    std::optional<Price>     spread;
    Timestamp                timestamp;
};

class OrderBook {
public:
    explicit OrderBook(Symbol symbol, IEventHandler& handler);

    ~OrderBook() = default;

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    ExecutionReport submit(const NewOrderRequest& req);
    ExecutionReport cancel(const CancelOrderRequest& req);
    ExecutionReport modify(const ModifyOrderRequest& req);

    [[nodiscard]] std::optional<Price>    best_bid()     const noexcept;
    [[nodiscard]] std::optional<Price>    best_ask()     const noexcept;
    [[nodiscard]] std::optional<Price>    mid_price()    const noexcept;
    [[nodiscard]] std::optional<Price>    spread()       const noexcept;
    [[nodiscard]] Quantity                bid_qty_at(Price p) const noexcept;
    [[nodiscard]] Quantity                ask_qty_at(Price p) const noexcept;
    [[nodiscard]] MarketDepth             depth(std::size_t levels = 10) const;
    [[nodiscard]] const Order*            find_order(OrderId id)         const noexcept;
    [[nodiscard]] const BookStats&        stats()                        const noexcept;
    [[nodiscard]] const Symbol&           symbol()                       const noexcept;
    [[nodiscard]] std::size_t             order_count()                  const noexcept;
    [[nodiscard]] std::size_t             bid_level_count()              const noexcept;
    [[nodiscard]] std::size_t             ask_level_count()              const noexcept;

    void reset() noexcept;

private:
    using BidSide = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskSide = std::map<Price, PriceLevel, std::less<Price>>;

    struct OrderEntry {
        Order*      order{nullptr};
        OrderNode*  node{nullptr};
    };

    Symbol              symbol_;
    IEventHandler&      handler_;
    BidSide             bids_;
    AskSide             asks_;

    std::unordered_map<OrderId, OrderEntry>  order_index_;
    std::atomic<std::uint64_t>               trade_sequence_{0};
    BookStats                                stats_;

    PoolAllocator<Order,     kMaxOrders>     order_pool_;
    PoolAllocator<OrderNode, kMaxOrderNodes> node_pool_;

    RejectReason validate(const NewOrderRequest& req) const noexcept;

    void process_limit_order(Order* order,
                             std::vector<Trade>& trades);
    void process_market_order(Order* order,
                              std::vector<Trade>& trades);

    template<typename BookSide>
    void match_order(Order* taker,
                     BookSide& side,
                     std::vector<Trade>& trades);

    void insert_resting(Order* order);
    bool remove_resting(OrderId id);

    Trade make_trade(const Order& maker,
                     const Order& taker,
                     Quantity qty) noexcept;

    void apply_fill(Order& maker, Order& taker,
                    const Trade& trade,
                    PriceLevel& level,
                    OrderNode* maker_node) noexcept;

    void rollback_fills(std::span<const Trade> trades) noexcept;

    Order* alloc_order(const NewOrderRequest& req, Timestamp ts);
    void   free_order(Order* o) noexcept;

    OrderNode* alloc_node(Order* o) noexcept;
    void       free_node(OrderNode* n) noexcept;
};

} 
