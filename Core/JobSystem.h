#pragma once

#ifndef POTATO_JOBSYSTEM_H
#define POTATO_JOBSYSTEM_H

/**
 * JobSystem — 工作分派 thread pool + ParallelFor
 *
 * 對應 EngineConfig::enableJobSystem / workerThreads：PotatoEngine
 * 初始化時建立實體（GetJobSystem()），關閉時排空解構。
 *
 * 設計：
 * - MPMC 佇列（deque + mutex + condition_variable）；遊戲規模下
 *   無鎖竊取的收益遠低於其複雜度，故不引入 work-stealing deques
 * - ParallelFor 的「等待」由呼叫執行緒參與執行（help-run）：
 *   workers 全忙時不會自鎖，也縮短尾部等待
 * - 巢狀安全：工作內可再 Submit/ParallelFor（help-run 保證推進）
 * - 析構排空：剩餘工作執行完才 join workers
 *
 * 誠實性：無優先級、無取消、無親和性綁核——需要這些時再迭代。
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Potato {

class JobSystem {
public:
    using Job = std::function<void()>;
    // 索引工作：ParallelFor 對 [0,count) 每個索引呼叫一次
    using IndexedJob = std::function<void(uint32_t)>;

    // workerCount=0 → hardware_concurrency()-1（保底 1）;
    // 顯式傳 0 且硬體回報 0 時退為「呼叫執行緒執行」模式（仍可運作）
    explicit JobSystem(uint32_t workerCount = 0);
    ~JobSystem();  // 排空佇列後 join 所有 worker

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // 投遞單一工作（非阻塞；由空閒 worker 或 help-run 執行緒接手）
    void Submit(Job job);

    // 批次並行：[0,count) 依 batchSize 粒度切塊投遞,函數返回時
    // 保證全部完成。batchSize=0 → 自動（約 count/(workers*8)）。
    // fn 必須對各索引獨立安全（呼叫端負責無資料競爭）。
    void ParallelFor(uint32_t count, const IndexedJob& fn,
                     uint32_t batchSize = 0);

    // 等待佇列清空且無進行中工作（等待期間本執行緒也幫忙執行）
    void WaitIdle();

    uint32_t WorkerCount() const { return workerCount; }
    bool IsRunning() const { return running.load(std::memory_order_acquire); }
    // 快照值：佇列中 + 執行中的工作數（僅供診斷,勿做同步依據）
    uint64_t InFlightCount() const {
        return inFlight.load(std::memory_order_acquire);
    }

private:
    void WorkerLoop();
    bool TryRunOne();  // 取一件工作執行；佇列空回 false

    std::deque<Job> queue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    uint32_t workerCount = 0;
    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> inFlight{0};  // 已投遞未完成（含執行中）
};

} // namespace Potato

#endif // POTATO_JOBSYSTEM_H
