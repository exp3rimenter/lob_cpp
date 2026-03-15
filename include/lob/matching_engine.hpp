#pragma once

#include "order_book.hpp"
#include "event_handler.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace lob {

class MatchingEngine {
public:
    explicit MatchingEngine(IEventHandler& handler);
    ~MatchingEngine() = default;

    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    bool add_instrument(const Symbol& symbol);
    bool remove_instrument(const Symbol& symbol);
    [[nodiscard]] bool has_instrument(const Symbol& symbol) const noexcept;

    ExecutionReport submit(const NewOrderRequest& req);
    ExecutionReport cancel(const CancelOrderRequest& req);
    ExecutionReport modify(const ModifyOrderRequest& req);

    [[nodiscard]] const OrderBook* book(const Symbol& symbol) const noexcept;
    [[nodiscard]] MarketDepth      depth(const Symbol& symbol,
                                        std::size_t levels = 10) const;

    [[nodiscard]] std::vector<Symbol> instruments() const;

private:
    IEventHandler&                                   handler_;
    mutable std::shared_mutex                        mutex_;
    std::unordered_map<Symbol, std::unique_ptr<OrderBook>> books_;

    OrderBook* find_book(const Symbol& symbol) noexcept;
    const OrderBook* find_book(const Symbol& symbol) const noexcept;
};

} 
