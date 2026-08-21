#pragma once
#include <pthread.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <borealis/core/application.hpp>
#include <borealis/core/time.hpp>

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
    void workerLoop() {
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
    }

    TaskQueue() : running_(true) {
        for (int i = 0; i < 4; i++) {
            pthread_t thread;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
            pthread_create(&thread, &attr, [](void* arg) -> void* {
                static_cast<TaskQueue*>(arg)->workerLoop();
                return nullptr;
            }, this);
            pthread_attr_destroy(&attr);
            threads_.push_back(thread);
        }
    }

    ~TaskQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cond_.notify_all();
        for (auto t : threads_) {
            pthread_join(t, nullptr);
        }
    }

    std::vector<pthread_t> threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
};

class PosterTextureCache {
public:
    static constexpr size_t MAX = 96;

    static int find(const std::string& url) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = textures_.find(url);
        if (it == textures_.end()) return -1;
        for (size_t i = 0; i < order_.size(); i++) {
            if (order_[i] == url) {
                order_.erase(order_.begin() + i);
                order_.push_back(url);
                break;
            }
        }
        return it->second;
    }

    static int put(const std::string& url, int tex) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = textures_.find(url);
        if (it != textures_.end()) return it->second;
        textures_[url] = tex;
        order_.push_back(url);
        return 0;
    }

    static int evictIfNeeded() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (textures_.size() <= MAX) return 0;
        std::string old = order_.front();
        order_.erase(order_.begin());
        auto it = textures_.find(old);
        if (it == textures_.end()) return 0;
        int tex = it->second;
        textures_.erase(it);
        return tex;
    }

private:
    static inline std::mutex mutex_;
    static inline std::vector<std::string> order_;
    static inline std::unordered_map<std::string, int> textures_;
};

// True right after an activity transition (push/pop/dialog): GPU texture
// uploads during this window collide with the first render of the new
// activity on fragile host drivers, so poster uploads are deferred.
static inline bool PosterUploadDeferred() {
    return (brls::getCPUTimeUsec() - brls::Application::getLastInputBlockTimeUsec()) < 150000ull;
}