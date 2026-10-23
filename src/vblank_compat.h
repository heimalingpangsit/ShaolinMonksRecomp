#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace shaolin {

// The Xbox API waits until the NEXT vertical blank. A fixed 16ms sleep is
// incorrect when the title reaches the call partway through a refresh.
class VBlankClock {
public:
    void Tick() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++count_;
        }
        cv_.notify_all();
    }
    bool WaitNext(std::chrono::milliseconds max_wait = std::chrono::milliseconds(25)) {
        std::unique_lock<std::mutex> guard(mutex_);
        const auto before = count_;
        return cv_.wait_for(guard, max_wait, [&] { return count_ != before; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    uint64_t count_ = 0;
};

}  // namespace shaolin
