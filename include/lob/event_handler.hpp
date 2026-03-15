#pragma once

#include "order.hpp"

namespace lob {

class IEventHandler {
public:
    virtual ~IEventHandler() = default;

    virtual void on_order_accepted(const Order& order)              = 0;
    virtual void on_order_rejected(const Order& order,
                                   RejectReason reason)             = 0;
    virtual void on_order_filled(const Order& order,
                                 const Trade& trade)                = 0;
    virtual void on_order_partially_filled(const Order& order,
                                           const Trade& trade)      = 0;
    virtual void on_order_cancelled(const Order& order)             = 0;
    virtual void on_trade(const Trade& trade)                       = 0;
    virtual void on_book_update(std::string_view symbol,
                                Side side, Price price,
                                Quantity qty)                       = 0;
};

class NullEventHandler final : public IEventHandler {
public:
    void on_order_accepted(const Order&)                        override {}
    void on_order_rejected(const Order&, RejectReason)          override {}
    void on_order_filled(const Order&, const Trade&)            override {}
    void on_order_partially_filled(const Order&, const Trade&)  override {}
    void on_order_cancelled(const Order&)                       override {}
    void on_trade(const Trade&)                                 override {}
    void on_book_update(std::string_view, Side, Price, Quantity) override {}
};

} 
