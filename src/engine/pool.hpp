#pragma once
#include <cassert>
#include <coroutine>
#include <mutex>
#include <vector>
#include <thread>
#include <optional>
#include <queue>

template <typename T>
struct PromiseBase {
    constexpr void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
    }
    std::optional<T> value_{};
    T& result() {
        return value_.value();
    }
    T extract() {
        auto val = std::move(*value_);
        value_ = std::nullopt;
        return val;
    }
};

template <>
struct PromiseBase<void> {
    static constexpr void return_void() noexcept {}
    constexpr void result() noexcept{}
};

struct [[nodiscard("co_await it!")]] transfer_control_to {
    std::coroutine_handle<> waiter;

    bool await_ready() const noexcept {
        assert(waiter != nullptr);
        return false;
    }
     std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
        return waiter;  // symmetric transfer here
    }
    static constexpr void await_resume() noexcept {
    }
};


template <typename T>
struct Promise : PromiseBase<T>{
    std::coroutine_handle<> waiter;
    static constexpr std::suspend_always initial_suspend() noexcept {
        return {};
    }
    auto get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }
    void unhandled_exception() const noexcept {
        std::terminate();
    }
    auto final_suspend() noexcept {
        // who_waits always setted because task not started or co_awaited
        return transfer_control_to(waiter);
    }
};

template <typename T>
struct Task {
    using promise_type = Promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    Task(handle_type handle) : handle_(handle)  {
    }
    struct remember_waiter_and_start_task_t {
        handle_type task_handle;

        bool await_ready() const noexcept {
            assert(task_handle != nullptr && !task_handle.done());
            return false;
        }
        std::coroutine_handle<void> await_suspend(
            std::coroutine_handle<void> handle) const noexcept {
            task_handle.promise().waiter = handle;
            // symmetric transfer control to task
            return task_handle;
        }
        [[nodiscard]] T await_resume() {
            return task_handle.promise().result();
        }
    };
    auto operator co_await() {
        return remember_waiter_and_start_task_t{handle_};
    }
     handle_type handle_{};
};

struct JobPromise;

struct Job {
    using promise_type = JobPromise;
    using handle_type = std::coroutine_handle<promise_type>;

    handle_type handle = nullptr;
    constexpr Job() noexcept = default;
    constexpr Job(handle_type h) noexcept : handle(h) {
    }
};

struct JobPromise {
    static constexpr std::suspend_never initial_suspend() noexcept {
        return {};
    }
    static constexpr std::suspend_never final_suspend() noexcept {
        return {};
    }
    Job get_return_object() noexcept {
        return {};
    }
    static constexpr void return_void() noexcept {
    }
    [[noreturn]] static void unhandled_exception() noexcept {
        std::terminate();
    }
};


#define ever (;;)

namespace engine {
    struct awaiter_t;
    struct Worker {
        std::mutex mut_tasks;
        std::condition_variable que_not_empty;
        std::queue<awaiter_t*> tasks;
        std::thread t;
    };
    struct awaiter_t {
        union {
            std::coroutine_handle<> handle;
            Worker* worker;
        };
        awaiter_t* next = nullptr;
        static bool await_ready() noexcept {
            return false;
        }
        void await_suspend(std::coroutine_handle<> handle_) noexcept {
            {
                std::lock_guard lg(worker->mut_tasks);
                worker->tasks.push(this);
                worker->que_not_empty.notify_one();
            }
            handle = handle_;
        }
        void await_resume() noexcept {
        }
    };

    struct pool {
        awaiter_t Shedule() {
            assert(!workers_.empty());
            std::size_t i = std::rand() % workers_.size();
            return awaiter_t{
                .worker = &workers_[i]
            };
        }
        void Shedule(auto&& fnc) {
            [](pool* pool, auto fnc)->Job {
              co_await pool->Shedule();
                fnc();
            }(this, std::forward<decltype(fnc)>(fnc));
        }
        pool(std::size_t size = std::thread::hardware_concurrency() - 1) {
            assert(size > 0);
            workers_.resize(size);
            for (std::size_t i = 0; i < size ; i++) {
                auto& this_worker = workers_[i];
                auto work = [this, &this_worker]() {
                    for ever {
                        std::unique_lock lock(this_worker.mut_tasks);
                        this_worker.que_not_empty.wait(lock, [&]() {
                            return !this_worker.tasks.empty() || this->stop_source_.stop_requested();
                        });
                        if (this->stop_source_.stop_requested()) {
                            return;
                        }
                        auto pulled_tasks = std::move(this_worker.tasks);
                        lock.unlock();
                        while (!pulled_tasks.empty()) {
                            if (auto task = pulled_tasks.front(); !task->handle.done()) {
                                task->handle.resume();
                            }
                            pulled_tasks.pop();
                        }
                    }
                };
                this_worker.t = std::thread(work);
            }
        }
        ~pool() {
            stop_source_.request_stop();
            for (auto& w : workers_) {
                w.que_not_empty.notify_one();
            }
            for (auto& w: workers_) {
                w.t.join();
            }
        }
    private:
        std::stop_source stop_source_;
        std::deque<Worker> workers_;
    };
}

#undef ever