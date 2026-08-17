#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>

class TaskQueue {
public:
    static TaskQueue& getInstance() {
        static TaskQueue instance;
        return instance;
    }

    void push(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(task));
        }
        cond_.notify_one();
    }

private:
    TaskQueue() : running_(true) {
        // Start 4 worker threads
        for(int i = 0; i < 4; i++) {
            workers_.emplace_back([this]() {
                while (running_) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cond_.wait(lock, [this]() { return !queue_.empty() || !running_; });
                        if (!running_ && queue_.empty()) return;
                        task = std::move(queue_.front());
                        queue_.pop();
                    }
                    task();
                }
            });
        }
    }
    
    ~TaskQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cond_.notify_all();
        for(auto& w : workers_) {
            if(w.joinable()) w.join();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
};
