#pragma once

#include "types.hpp"
#include <atomic>
#include <chrono>

namespace lob {

class Clock {
public:
    [[nodiscard]] static Timestamp now() noexcept {
        return static_cast<Timestamp>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
    }
};

class MockClock {
public:
    [[nodiscard]] Timestamp now() const noexcept {
        return current_.load(std::memory_order_acquire);
    }

    void advance(Timestamp delta) noexcept {
        current_.fetch_add(delta, std::memory_order_acq_rel);
    }

    void set(Timestamp t) noexcept {
        current_.store(t, std::memory_order_release);
    }

private:
    std::atomic<Timestamp> current_{0};
};

} 
