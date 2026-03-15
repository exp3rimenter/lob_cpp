#pragma once

#include "order.hpp"
#include <cassert>
#include <cstddef>

namespace lob {

struct OrderNode {
    Order*      order{nullptr};
    OrderNode*  prev{nullptr};
    OrderNode*  next{nullptr};
};

class PriceLevel {
public:
    explicit PriceLevel(Price p) noexcept : price_(p) {}

    PriceLevel(const PriceLevel&)            = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;

    void push_back(OrderNode* node) noexcept {
        assert(node);
        node->prev = tail_;
        node->next = nullptr;
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        total_quantity_ += node->order->remaining();
        ++count_;
    }

    void remove(OrderNode* node) noexcept {
        assert(node);
        if (node->prev) node->prev->next = node->next;
        else            head_            = node->next;
        if (node->next) node->next->prev = node->prev;
        else            tail_            = node->prev;
        node->prev = nullptr;
        node->next = nullptr;
        total_quantity_ -= node->order->remaining();
        --count_;
    }

    void reduce_quantity(Quantity qty) noexcept {
        assert(total_quantity_ >= qty);
        total_quantity_ -= qty;
    }

    [[nodiscard]] OrderNode* front()  const noexcept { return head_; }
    [[nodiscard]] bool       empty()  const noexcept { return head_ == nullptr; }
    [[nodiscard]] Quantity   total_quantity() const noexcept { return total_quantity_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] Price      price()  const noexcept { return price_; }

private:
    Price      price_;
    Quantity   total_quantity_{0};
    std::size_t count_{0};
    OrderNode* head_{nullptr};
    OrderNode* tail_{nullptr};
};

} 
