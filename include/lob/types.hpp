#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <chrono>

namespace lob {

using OrderId   = std::uint64_t;
using Price     = std::int64_t;
using Quantity  = std::uint64_t;
using Timestamp = std::uint64_t;
using Symbol    = std::string;

static constexpr Price     kInvalidPrice    = std::numeric_limits<Price>::min();
static constexpr Quantity  kInvalidQuantity = 0;
static constexpr OrderId   kInvalidOrderId  = 0;

enum class Side : std::uint8_t {
    Buy  = 0,
    Sell = 1,
};

enum class OrderType : std::uint8_t {
    Limit  = 0,
    Market = 1,
    IOC    = 2,
    FOK    = 3,
};

enum class OrderStatus : std::uint8_t {
    New              = 0,
    PartiallyFilled  = 1,
    Filled           = 2,
    Cancelled        = 3,
    Rejected         = 4,
};

enum class TimeInForce : std::uint8_t {
    GTC = 0,
    IOC = 1,
    FOK = 2,
    DAY = 3,
};

enum class RejectReason : std::uint8_t {
    None              = 0,
    InvalidPrice      = 1,
    InvalidQuantity   = 2,
    InvalidSymbol     = 3,
    DuplicateOrderId  = 4,
    OrderNotFound     = 5,
    OrderNotActive    = 6,
    InsufficientDepth = 7,
};

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "Buy" : "Sell";
}

[[nodiscard]] constexpr std::string_view to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit:  return "Limit";
        case OrderType::Market: return "Market";
        case OrderType::IOC:    return "IOC";
        case OrderType::FOK:    return "FOK";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::New:             return "New";
        case OrderStatus::PartiallyFilled: return "PartiallyFilled";
        case OrderStatus::Filled:          return "Filled";
        case OrderStatus::Cancelled:       return "Cancelled";
        case OrderStatus::Rejected:        return "Rejected";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:              return "None";
        case RejectReason::InvalidPrice:      return "InvalidPrice";
        case RejectReason::InvalidQuantity:   return "InvalidQuantity";
        case RejectReason::InvalidSymbol:     return "InvalidSymbol";
        case RejectReason::DuplicateOrderId:  return "DuplicateOrderId";
        case RejectReason::OrderNotFound:     return "OrderNotFound";
        case RejectReason::OrderNotActive:    return "OrderNotActive";
        case RejectReason::InsufficientDepth: return "InsufficientDepth";
    }
    return "Unknown";
}

} 
