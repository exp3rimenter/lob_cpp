#pragma once

#include "types.hpp"
#include <cassert>

namespace lob {

struct alignas(64) Order {
    OrderId    id;
    Price      price;
    Quantity   quantity;
    Quantity   filled_quantity;
    Timestamp  timestamp;
    Symbol     symbol;
    Side       side;
    OrderType  type;
    OrderStatus status;
    TimeInForce tif;

    [[nodiscard]] Quantity remaining() const noexcept {
        assert(quantity >= filled_quantity);
        return quantity - filled_quantity;
    }

    [[nodiscard]] bool is_active() const noexcept {
        return status == OrderStatus::New ||
               status == OrderStatus::PartiallyFilled;
    }

    [[nodiscard]] bool is_terminal() const noexcept {
        return !is_active();
    }

    [[nodiscard]] bool is_buy()  const noexcept { return side == Side::Buy; }
    [[nodiscard]] bool is_sell() const noexcept { return side == Side::Sell; }
};

struct Trade {
    OrderId   maker_order_id;
    OrderId   taker_order_id;
    Symbol    symbol;
    Price     price;
    Quantity  quantity;
    Side      aggressor_side;
    Timestamp timestamp;
    std::uint64_t sequence_number;
};

struct ExecutionReport {
    OrderId     order_id;
    OrderStatus status;
    Price       price;
    Quantity    filled_qty;
    Quantity    remaining_qty;
    Timestamp   timestamp;
    RejectReason reject_reason;
};

struct NewOrderRequest {
    OrderId     client_order_id;
    Symbol      symbol;
    Side        side;
    OrderType   type;
    TimeInForce tif;
    Price       price;
    Quantity    quantity;
};

struct CancelOrderRequest {
    OrderId order_id;
    Symbol  symbol;
};

struct ModifyOrderRequest {
    OrderId  order_id;
    Symbol   symbol;
    Price    new_price;
    Quantity new_quantity;
};

} 
