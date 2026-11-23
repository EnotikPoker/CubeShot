#pragma once
#include "game/BotNavigation.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace cubeshot
{
// A single cancellable worker owns immutable copies, never the live World or global caches.
class NavigationWorker
{
public:
    NavigationWorker();
    ~NavigationWorker();
    void cancel();
    std::uint64_t request(Arena arena, Abilities abilities);
    std::shared_ptr<const BotNavigation> take(std::uint64_t generation);

private:
    struct Job
    {
        Arena arena;
        Abilities abilities;
        std::uint64_t generation;
        std::shared_ptr<std::atomic_bool> cancel;
    };

    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::optional<Job> pending_;
    std::shared_ptr<std::atomic_bool> cancel_;
    std::shared_ptr<const BotNavigation> result_;
    std::uint64_t next_ = 0, resultGeneration_ = 0;
    std::thread thread_;
    void run();
};
} // namespace cubeshot
