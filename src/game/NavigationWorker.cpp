#include "game/NavigationWorker.h"

namespace cubeshot
{
NavigationWorker::NavigationWorker() : thread_([this] { run(); }) {}

NavigationWorker::~NavigationWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        if (cancel_)
            cancel_->store(true);
    }
    wake_.notify_one();
    thread_.join();
}

void NavigationWorker::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_)
        cancel_->store(true);
    pending_.reset();
    result_.reset();
    ++next_;
}

std::uint64_t NavigationWorker::request(Arena arena, Abilities abilities)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancel_)
        cancel_->store(true);
    cancel_ = std::make_shared<std::atomic_bool>(false);
    pending_ = Job{std::move(arena), abilities, ++next_, cancel_};
    wake_.notify_one();
    return next_;
}

std::shared_ptr<const BotNavigation> NavigationWorker::take(std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (resultGeneration_ != generation)
        return {};
    return std::move(result_);
}

void NavigationWorker::run()
{
    for (;;)
    {
        std::optional<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || pending_.has_value(); });
            if (stop_)
                return;
            job = std::move(pending_);
            pending_.reset();
        }
        auto graph = std::make_shared<const BotNavigation>(job->arena, job->abilities, job->cancel);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job->cancel->load() && !stop_)
        {
            result_ = std::move(graph);
            resultGeneration_ = job->generation;
        }
    }
}
} // namespace cubeshot
