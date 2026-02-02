#pragma once


#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

class ThreadPool {
public:
    using Task = std::function<void()>;

    ThreadPool(size_t num_threads);
    ~ThreadPool();

    void enqueue(Task task);
    void stop();

private:
    void worker_thread();

    std::vector<std::thread> workers;
    std::queue<Task> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop_{ false };
};