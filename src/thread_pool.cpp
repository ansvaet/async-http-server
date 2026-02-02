#include "thread_pool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::worker_thread() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this]() {
                return stop_ || !tasks.empty();
                });

            if (stop_ && tasks.empty()) {
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        try {
            task();
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] Exception in worker thread: " << e.what() << std::endl;
        }
    }
}

void ThreadPool::enqueue(Task task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (stop_) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks.emplace(std::move(task));
    }
    condition.notify_one();
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop_ = true;
    }
    condition.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}