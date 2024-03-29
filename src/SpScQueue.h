#pragma once

#include <deque>
#include <mutex>
#include <optional>
#include <condition_variable>

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

template<typename T>
struct SpScQueue {
    std::deque<T> data;
    std::mutex m;
    std::condition_variable cv;

    std::optional<T> pop_non_blocking() {
        std::scoped_lock<std::mutex> lock(m);
        if(!data.empty()) {
            T tmp = data.front();
            data.pop_front();
            return tmp;
        }
        return {};
    }

    T pop_blocking() {
        std::unique_lock<std::mutex> lock(m);
        while(data.empty()) {
            cv.wait(lock);
        }
        T tmp = data.front();
        data.pop_front();
        return tmp;
    }

    T pop_blocking(uint64_t timeoutMillis) {
        std::unique_lock<std::mutex> lock(m);
        while (data.empty()) {
            cv.wait_for(lock, std::chrono::milliseconds(timeoutMillis));
        }
        T tmp = data.front();
        data.pop_front();
        return tmp;
    }

    void push(T t) {
        std::scoped_lock<std::mutex> lock(m);
        data.push_back(t);
        if(data.size() == 1) {
            cv.notify_all();
        }
    }
};
