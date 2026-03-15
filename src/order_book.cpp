#include "lob/order_book.hpp"
#include "lob/clock.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <span>

namespace lob {

OrderBook::OrderBook(Symbol symbol, IEventHandler& handler)
    : symbol_(std::move(symbol))
    , handler_(handler)
{
    order_index_.reserve(1024);
}

ExecutionReport OrderBook::submit(const NewOrderRequest& req) {
    const auto ts  = Clock::now();
    RejectReason reason = validate(req);

    if (reason != RejectReason::None) {
        Order dummy{};
        dummy.id     = req.client_order_id;
        dummy.symbol = req.symbol;
        dummy.side   = req.side;
        dummy.type   = req.type;
        dummy.price  = req.price;
        dummy.quantity = req.quantity;
        dummy.status = OrderStatus::Rejected;
        ++stats_.orders_rejected;
        handler_.on_order_rejected(dummy, reason);
        return ExecutionReport{
            .order_id      = req.client_order_id,
            .status        = OrderStatus::Rejected,
            .price         = req.price,
            .filled_qty    = 0,
            .remaining_qty = req.quantity,
            .timestamp     = ts,
            .reject_reason = reason,
        };
    }

    Order* order = alloc_order(req, ts);
    if (!order) {
        ++stats_.orders_rejected;
        handler_.on_order_rejected(*order, RejectReason::InvalidQuantity);
        return ExecutionReport{
            .order_id      = req.client_order_id,
            .status        = OrderStatus::Rejected,
            .price         = req.price,
            .filled_qty    = 0,
            .remaining_qty = req.quantity,
            .timestamp     = ts,
            .reject_reason = RejectReason::InvalidQuantity,
        };
    }

    ++stats_.orders_added;
    handler_.on_order_accepted(*order);

    std::vector<Trade> trades;
    trades.reserve(8);

    if (order->type == OrderType::Market) {
        process_market_order(order, trades);
    } else {
        process_limit_order(order, trades);
    }

    return ExecutionReport{
        .order_id      = order->id,
        .status        = order->status,
        .price         = order->price,
        .filled_qty    = order->filled_quantity,
        .remaining_qty = order->remaining(),
        .timestamp     = ts,
        .reject_reason = RejectReason::None,
    };
}

ExecutionReport OrderBook::cancel(const CancelOrderRequest& req) {
    const auto ts = Clock::now();

    auto it = order_index_.find(req.order_id);
    if (it == order_index_.end()) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = OrderStatus::Rejected,
            .price         = kInvalidPrice,
            .filled_qty    = 0,
            .remaining_qty = 0,
            .timestamp     = ts,
            .reject_reason = RejectReason::OrderNotFound,
        };
    }

    Order* order = it->second.order;
    if (!order->is_active()) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = order->status,
            .price         = order->price,
            .filled_qty    = order->filled_quantity,
            .remaining_qty = order->remaining(),
            .timestamp     = ts,
            .reject_reason = RejectReason::OrderNotActive,
        };
    }

    remove_resting(req.order_id);
    order->status = OrderStatus::Cancelled;
    ++stats_.orders_cancelled;
    handler_.on_order_cancelled(*order);

    const auto snap = ExecutionReport{
        .order_id      = order->id,
        .status        = order->status,
        .price         = order->price,
        .filled_qty    = order->filled_quantity,
        .remaining_qty = 0,
        .timestamp     = ts,
        .reject_reason = RejectReason::None,
    };

    free_order(order);
    return snap;
}

ExecutionReport OrderBook::modify(const ModifyOrderRequest& req) {
    const auto ts = Clock::now();

    auto it = order_index_.find(req.order_id);
    if (it == order_index_.end()) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = OrderStatus::Rejected,
            .price         = kInvalidPrice,
            .filled_qty    = 0,
            .remaining_qty = 0,
            .timestamp     = ts,
            .reject_reason = RejectReason::OrderNotFound,
        };
    }

    Order* order = it->second.order;
    if (!order->is_active()) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = order->status,
            .price         = order->price,
            .filled_qty    = order->filled_quantity,
            .remaining_qty = order->remaining(),
            .timestamp     = ts,
            .reject_reason = RejectReason::OrderNotActive,
        };
    }

    if (req.new_quantity == 0 || req.new_quantity < order->filled_quantity) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = OrderStatus::Rejected,
            .price         = order->price,
            .filled_qty    = order->filled_quantity,
            .remaining_qty = order->remaining(),
            .timestamp     = ts,
            .reject_reason = RejectReason::InvalidQuantity,
        };
    }

    remove_resting(req.order_id);

    order->price    = req.new_price;
    order->quantity = req.new_quantity;
    order->status   = (order->filled_quantity > 0)
        ? OrderStatus::PartiallyFilled
        : OrderStatus::New;

    std::vector<Trade> trades;
    trades.reserve(4);
    process_limit_order(order, trades);

    return ExecutionReport{
        .order_id      = order->id,
        .status        = order->status,
        .price         = order->price,
        .filled_qty    = order->filled_quantity,
        .remaining_qty = order->remaining(),
        .timestamp     = ts,
        .reject_reason = RejectReason::None,
    };
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::mid_price() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return (*bb + *ba) / 2;
}

std::optional<Price> OrderBook::spread() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return *ba - *bb;
}

Quantity OrderBook::bid_qty_at(Price p) const noexcept {
    auto it = bids_.find(p);
    return it != bids_.end() ? it->second.total_quantity() : 0;
}

Quantity OrderBook::ask_qty_at(Price p) const noexcept {
    auto it = asks_.find(p);
    return it != asks_.end() ? it->second.total_quantity() : 0;
}

MarketDepth OrderBook::depth(std::size_t levels) const {
    MarketDepth md;
    md.timestamp = Clock::now();
    md.best_bid  = best_bid();
    md.best_ask  = best_ask();
    md.mid_price = mid_price();
    md.spread    = spread();

    md.bids.reserve(levels);
    for (auto it = bids_.begin(); it != bids_.end() && md.bids.size() < levels; ++it) {
        md.bids.push_back({it->first, it->second.total_quantity(), it->second.count()});
    }

    md.asks.reserve(levels);
    for (auto it = asks_.begin(); it != asks_.end() && md.asks.size() < levels; ++it) {
        md.asks.push_back({it->first, it->second.total_quantity(), it->second.count()});
    }

    return md;
}

const Order* OrderBook::find_order(OrderId id) const noexcept {
    auto it = order_index_.find(id);
    return it != order_index_.end() ? it->second.order : nullptr;
}

const BookStats& OrderBook::stats()       const noexcept { return stats_; }
const Symbol&    OrderBook::symbol()      const noexcept { return symbol_; }
std::size_t OrderBook::order_count()      const noexcept { return order_index_.size(); }
std::size_t OrderBook::bid_level_count()  const noexcept { return bids_.size(); }
std::size_t OrderBook::ask_level_count()  const noexcept { return asks_.size(); }

void OrderBook::reset() noexcept {
    for (auto& [id, entry] : order_index_) {
        free_node(entry.node);
        free_order(entry.order);
    }
    order_index_.clear();
    bids_.clear();
    asks_.clear();
    stats_ = {};
    trade_sequence_.store(0, std::memory_order_relaxed);
}

RejectReason OrderBook::validate(const NewOrderRequest& req) const noexcept {
    if (req.symbol.empty())     return RejectReason::InvalidSymbol;
    if (req.quantity == 0)      return RejectReason::InvalidQuantity;
    if (req.type == OrderType::Limit && req.price <= 0)
                                return RejectReason::InvalidPrice;
    if (order_index_.count(req.client_order_id))
                                return RejectReason::DuplicateOrderId;
    return RejectReason::None;
}

void OrderBook::process_limit_order(Order* order, std::vector<Trade>& trades) {
    if (order->side == Side::Buy) {
        match_order(order, asks_, trades);
    } else {
        match_order(order, bids_, trades);
    }

    if (!order->is_active()) return;

    if (order->type == OrderType::IOC ||
        order->tif  == TimeInForce::IOC) {
        order->status = OrderStatus::Cancelled;
        ++stats_.orders_cancelled;
        handler_.on_order_cancelled(*order);
        free_order(order);
        return;
    }

    if (order->type == OrderType::FOK ||
        order->tif  == TimeInForce::FOK) {
        if (order->filled_quantity == 0) {
            order->status = OrderStatus::Cancelled;
            ++stats_.orders_cancelled;
            rollback_fills(trades);
            trades.clear();
            free_order(order);
        } else if (order->remaining() > 0) {
            order->status = OrderStatus::Cancelled;
            rollback_fills(trades);
            trades.clear();
            free_order(order);
        }
        return;
    }

    insert_resting(order);
}

void OrderBook::process_market_order(Order* order, std::vector<Trade>& trades) {
    if (order->side == Side::Buy) {
        match_order(order, asks_, trades);
    } else {
        match_order(order, bids_, trades);
    }

    if (order->is_active()) {
        order->status = OrderStatus::Cancelled;
        handler_.on_order_cancelled(*order);
        free_order(order);
    }
}

template<typename BookSide>
void OrderBook::match_order(Order* taker, BookSide& side, std::vector<Trade>& trades) {
    while (!side.empty() && taker->remaining() > 0) {
        auto level_it = side.begin();
        PriceLevel& level = level_it->second;

        if constexpr (std::is_same_v<BookSide, AskSide>) {
            if (taker->type == OrderType::Limit &&
                taker->price < level.price()) break;
        } else {
            if (taker->type == OrderType::Limit &&
                taker->price > level.price()) break;
        }

        OrderNode* node = level.front();
        while (node && taker->remaining() > 0) {
            Order& maker   = *node->order;
            OrderNode* nxt = node->next;

            const Quantity qty = std::min(taker->remaining(), maker.remaining());
            Trade trade = make_trade(maker, *taker, qty);

            apply_fill(maker, *taker, trade, level, node);

            trades.push_back(trade);
            handler_.on_trade(trade);
            ++stats_.trades_executed;
            stats_.total_traded_quantity += qty;
            stats_.total_traded_notional += static_cast<std::uint64_t>(
                static_cast<Price>(qty) * maker.price);

            if (maker.status == OrderStatus::Filled) {
                ++stats_.orders_filled;
                handler_.on_order_filled(maker, trade);
                order_index_.erase(maker.id);
                free_node(node);
                free_order(&maker);
            } else {
                ++stats_.orders_partially_filled;
                handler_.on_order_partially_filled(maker, trade);
            }

            node = nxt;
        }

        if (level.empty()) {
            side.erase(level_it);
        }
    }

    if (taker->status == OrderStatus::Filled) {
        ++stats_.orders_filled;
        handler_.on_order_filled(*taker, trades.back());
    } else if (taker->filled_quantity > 0) {
        taker->status = OrderStatus::PartiallyFilled;
        ++stats_.orders_partially_filled;
        handler_.on_order_partially_filled(*taker, trades.back());
    }
}

void OrderBook::insert_resting(Order* order) {
    OrderNode* node = alloc_node(order);
    assert(node);

    if (order->side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(order->price, order->price);
        it->second.push_back(node);
        handler_.on_book_update(symbol_, Side::Buy, order->price,
                                it->second.total_quantity());
    } else {
        auto [it, inserted] = asks_.try_emplace(order->price, order->price);
        it->second.push_back(node);
        handler_.on_book_update(symbol_, Side::Sell, order->price,
                                it->second.total_quantity());
    }

    order_index_.emplace(order->id, OrderEntry{order, node});
}

bool OrderBook::remove_resting(OrderId id) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) return false;

    OrderEntry entry = it->second;
    Order&     order = *entry.order;

    auto remove_from = [&](auto& side) {
        auto lvl_it = side.find(order.price);
        if (lvl_it == side.end()) return;
        lvl_it->second.remove(entry.node);
        handler_.on_book_update(symbol_, order.side, order.price,
                                lvl_it->second.total_quantity());
        if (lvl_it->second.empty()) {
            side.erase(lvl_it);
        }
    };

    if (order.side == Side::Buy) remove_from(bids_);
    else                         remove_from(asks_);

    free_node(entry.node);
    order_index_.erase(it);
    return true;
}

Trade OrderBook::make_trade(const Order& maker,
                            const Order& taker,
                            Quantity qty) noexcept {
    return Trade{
        .maker_order_id   = maker.id,
        .taker_order_id   = taker.id,
        .symbol           = symbol_,
        .price            = maker.price,
        .quantity         = qty,
        .aggressor_side   = taker.side,
        .timestamp        = Clock::now(),
        .sequence_number  = trade_sequence_.fetch_add(1, std::memory_order_relaxed),
    };
}

void OrderBook::apply_fill(Order& maker,
                           Order& taker,
                           const Trade& trade,
                           PriceLevel& level,
                           OrderNode* maker_node) noexcept {
    const Quantity qty = trade.quantity;

    maker.filled_quantity += qty;
    taker.filled_quantity += qty;

    maker.status = (maker.remaining() == 0)
        ? OrderStatus::Filled
        : OrderStatus::PartiallyFilled;

    taker.status = (taker.remaining() == 0)
        ? OrderStatus::Filled
        : OrderStatus::PartiallyFilled;

    level.reduce_quantity(qty);
    if (maker.status == OrderStatus::Filled) {
        level.remove(maker_node);
    }
}

void OrderBook::rollback_fills(std::span<const Trade> trades) noexcept {
    for (auto it = trades.rbegin(); it != trades.rend(); ++it) {
        auto maker_it = order_index_.find(it->maker_order_id);
        if (maker_it != order_index_.end()) {
            Order& m = *maker_it->second.order;
            m.filled_quantity -= it->quantity;
            m.status = (m.filled_quantity == 0)
                ? OrderStatus::New : OrderStatus::PartiallyFilled;
        }
    }
}

Order* OrderBook::alloc_order(const NewOrderRequest& req, Timestamp ts) {
    Order* o = order_pool_.allocate();
    if (!o) return nullptr;
    new (o) Order{
        .id              = req.client_order_id,
        .price           = req.price,
        .quantity        = req.quantity,
        .filled_quantity = 0,
        .timestamp       = ts,
        .symbol          = req.symbol,
        .side            = req.side,
        .type            = req.type,
        .status          = OrderStatus::New,
        .tif             = req.tif,
    };
    return o;
}

void OrderBook::free_order(Order* o) noexcept {
    if (!o) return;
    o->~Order();
    order_pool_.deallocate(o);
}

OrderNode* OrderBook::alloc_node(Order* o) noexcept {
    OrderNode* n = node_pool_.allocate();
    if (!n) return nullptr;
    n->order = o;
    n->prev  = nullptr;
    n->next  = nullptr;
    return n;
}

void OrderBook::free_node(OrderNode* n) noexcept {
    if (!n) return;
    node_pool_.deallocate(n);
}

} 
