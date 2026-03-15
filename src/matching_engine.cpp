#include "lob/matching_engine.hpp"

#include <algorithm>
#include <stdexcept>

namespace lob {

MatchingEngine::MatchingEngine(IEventHandler& handler)
    : handler_(handler)
{}

bool MatchingEngine::add_instrument(const Symbol& symbol) {
    std::unique_lock lock(mutex_);
    auto [it, inserted] = books_.try_emplace(
        symbol,
        std::make_unique<OrderBook>(symbol, handler_)
    );
    return inserted;
}

bool MatchingEngine::remove_instrument(const Symbol& symbol) {
    std::unique_lock lock(mutex_);
    return books_.erase(symbol) > 0;
}

bool MatchingEngine::has_instrument(const Symbol& symbol) const noexcept {
    std::shared_lock lock(mutex_);
    return books_.count(symbol) > 0;
}

ExecutionReport MatchingEngine::submit(const NewOrderRequest& req) {
    std::shared_lock lock(mutex_);
    auto* book = find_book(req.symbol);
    if (!book) {
        return ExecutionReport{
            .order_id      = req.client_order_id,
            .status        = OrderStatus::Rejected,
            .price         = req.price,
            .filled_qty    = 0,
            .remaining_qty = req.quantity,
            .timestamp     = 0,
            .reject_reason = RejectReason::InvalidSymbol,
        };
    }
    return book->submit(req);
}

ExecutionReport MatchingEngine::cancel(const CancelOrderRequest& req) {
    std::shared_lock lock(mutex_);
    auto* book = find_book(req.symbol);
    if (!book) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = OrderStatus::Rejected,
            .price         = kInvalidPrice,
            .filled_qty    = 0,
            .remaining_qty = 0,
            .timestamp     = 0,
            .reject_reason = RejectReason::InvalidSymbol,
        };
    }
    return book->cancel(req);
}

ExecutionReport MatchingEngine::modify(const ModifyOrderRequest& req) {
    std::shared_lock lock(mutex_);
    auto* book = find_book(req.symbol);
    if (!book) {
        return ExecutionReport{
            .order_id      = req.order_id,
            .status        = OrderStatus::Rejected,
            .price         = req.new_price,
            .filled_qty    = 0,
            .remaining_qty = 0,
            .timestamp     = 0,
            .reject_reason = RejectReason::InvalidSymbol,
        };
    }
    return book->modify(req);
}

const OrderBook* MatchingEngine::book(const Symbol& symbol) const noexcept {
    std::shared_lock lock(mutex_);
    return find_book(symbol);
}

MarketDepth MatchingEngine::depth(const Symbol& symbol, std::size_t levels) const {
    std::shared_lock lock(mutex_);
    const auto* b = find_book(symbol);
    if (!b) return {};
    return b->depth(levels);
}

std::vector<Symbol> MatchingEngine::instruments() const {
    std::shared_lock lock(mutex_);
    std::vector<Symbol> result;
    result.reserve(books_.size());
    for (const auto& [sym, _] : books_) {
        result.push_back(sym);
    }
    return result;
}

OrderBook* MatchingEngine::find_book(const Symbol& symbol) noexcept {
    auto it = books_.find(symbol);
    return it != books_.end() ? it->second.get() : nullptr;
}

const OrderBook* MatchingEngine::find_book(const Symbol& symbol) const noexcept {
    auto it = books_.find(symbol);
    return it != books_.end() ? it->second.get() : nullptr;
}

} 
