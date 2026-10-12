#include "JobSystem.h"

#include <algorithm>

namespace Potato {

JobSystem::JobSystem(uint32_t requestedWorkers) {
    uint32_t n = requestedWorkers;
    if (n == 0) {
        const uint32_t hw = std::thread::hardware_concurrency();
        n = (hw > 1) ? (hw - 1) : hw;  // hw==0（查不到）→ n=0,退為純呼叫端執行
    }
    workerCount = n;
    running.store(true, std::memory_order_release);
    workers.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        workers.emplace_back([this] { WorkerLoop(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lk(queueMutex);
        stopping.store(true, std::memory_order_release);
    }
    cv.notify_all();
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
    running.store(false, std::memory_order_release);
}

void JobSystem::Submit(Job job) {
    // 先計數再入佇列：若先 push,worker 可能搶在 fetch_add 前
    // 完成工作並 fetch_sub,造成 inFlight 暫態下溢
    inFlight.fetch_add(1, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(queueMutex);
        queue.push_back(std::move(job));
    }
    cv.notify_one();
}

bool JobSystem::TryRunOne() {
    Job job;
    {
        std::lock_guard<std::mutex> lk(queueMutex);
        if (queue.empty()) return false;
        job = std::move(queue.front());
        queue.pop_front();
    }
    job();
    inFlight.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

void JobSystem::WorkerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(queueMutex);
            cv.wait(lk, [&] {
                return stopping.load(std::memory_order_acquire) ||
                       !queue.empty();
            });
            if (queue.empty()) {
                // stopping 且佇列已排空 → 收工
                return;
            }
            job = std::move(queue.front());
            queue.pop_front();
        }
        job();
        inFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void JobSystem::ParallelFor(uint32_t count, const IndexedJob& fn,
                            uint32_t batchSize) {
    if (count == 0 || !fn) return;

    if (batchSize == 0) {
        const uint32_t chunks = std::max(1u, workerCount * 8u);
        batchSize = std::max(1u, count / chunks);
    }
    const uint32_t batches = (count + batchSize - 1) / batchSize;

    // 批次計數器放棧上：本函數等到全數完成才返回,參考存活保證
    std::atomic<uint32_t> remaining{batches};
    for (uint32_t b = 0; b < batches; ++b) {
        const uint32_t lo = b * batchSize;
        const uint32_t hi = std::min(count, lo + batchSize);
        Submit([&, lo, hi] {
            for (uint32_t i = lo; i < hi; ++i) fn(i);
            remaining.fetch_sub(1, std::memory_order_release);
        });
    }

    // help-run：呼叫端也執行工作直到批次歸零
    while (remaining.load(std::memory_order_acquire) != 0) {
        if (!TryRunOne()) std::this_thread::yield();
    }
}

void JobSystem::WaitIdle() {
    for (;;) {
        if (inFlight.load(std::memory_order_acquire) == 0) {
            std::lock_guard<std::mutex> lk(queueMutex);
            if (queue.empty() &&
                inFlight.load(std::memory_order_acquire) == 0) {
                return;
            }
        }
        if (!TryRunOne()) std::this_thread::yield();
    }
}

} // namespace Potato
