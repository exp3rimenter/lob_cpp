#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace lob {

template<typename T, std::size_t Capacity>
class PoolAllocator {
    static_assert(std::is_trivially_destructible_v<T> || true, "");
    static_assert(Capacity > 0);

public:
    using value_type = T;

    PoolAllocator() {
        for (std::size_t i = 0; i < Capacity - 1; ++i) {
            reinterpret_cast<FreeNode*>(&slots_[i])->next =
                reinterpret_cast<FreeNode*>(&slots_[i + 1]);
        }
        reinterpret_cast<FreeNode*>(&slots_[Capacity - 1])->next = nullptr;
        head_.store(reinterpret_cast<FreeNode*>(&slots_[0]),
                    std::memory_order_relaxed);
    }

    ~PoolAllocator() = default;

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    [[nodiscard]] T* allocate() noexcept {
        FreeNode* head = head_.load(std::memory_order_acquire);
        while (head) {
            if (head_.compare_exchange_weak(head, head->next,
                    std::memory_order_release, std::memory_order_acquire)) {
                allocated_.fetch_add(1, std::memory_order_relaxed);
                return reinterpret_cast<T*>(head);
            }
        }
        return nullptr;
    }

    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        auto* node = reinterpret_cast<FreeNode*>(ptr);
        FreeNode* head = head_.load(std::memory_order_acquire);
        do {
            node->next = head;
        } while (!head_.compare_exchange_weak(head, node,
                    std::memory_order_release, std::memory_order_acquire));
        allocated_.fetch_sub(1, std::memory_order_relaxed);
    }

    template<typename... Args>
    [[nodiscard]] T* construct(Args&&... args) noexcept(
            std::is_nothrow_constructible_v<T, Args...>) {
        T* ptr = allocate();
        if (!ptr) return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    void destroy(T* ptr) noexcept {
        if (!ptr) return;
        ptr->~T();
        deallocate(ptr);
    }

    [[nodiscard]] std::size_t capacity()  const noexcept { return Capacity; }
    [[nodiscard]] std::size_t allocated() const noexcept {
        return allocated_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t available() const noexcept {
        return Capacity - allocated();
    }

private:
    struct FreeNode { FreeNode* next; };

    static_assert(sizeof(T) >= sizeof(FreeNode),
        "Pool slot must be at least as large as a pointer");

    alignas(T) std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, Capacity> slots_;
    std::atomic<FreeNode*>    head_{nullptr};
    std::atomic<std::size_t>  allocated_{0};
};

} 
