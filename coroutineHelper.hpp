#ifndef __COROUTINE_HELPER_HPP__
#define __COROUTINE_HELPER_HPP__

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <cstdio>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// 线程池
class ThreadPool {
private:
    std::atomic<bool> stop;
    std::vector<std::thread> workers;
    std::deque<std::coroutine_handle<>> handles;
    std::unordered_map<std::coroutine_handle<>, std::unordered_set<std::coroutine_handle<>>> relationMap;
    std::condition_variable condition;
    std::mutex mutex;
public:
    ThreadPool(int n = 64) noexcept : stop(false) {
        for (int i = 0; i < n; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::coroutine_handle<> handle;
                    {
                        std::unique_lock<std::mutex> _lock {mutex};
                        condition.wait(_lock, [this] { return stop || !handles.empty(); });
                        if (stop) return;
                        handle = handles.front();
                        handles.pop_front();
                    }
                    handle.resume();
                }
            });
        }
    }
    void acquireLock() noexcept {
        mutex.lock();
    }
    void releaseLock() noexcept {
        mutex.unlock();
    }
    void notify_n(size_t n = 1) noexcept {
        for (size_t i = 0; i < n; ++i) condition.notify_one();
    }
    void enqueue(std::coroutine_handle<> handle, bool firstExecute = false) noexcept {
        // 注意: 该函数需要手动保护
        // 注意: 锁释放之后需要手动调用notify_one()
        if (stop) return;
        if (firstExecute) handles.push_front(handle);
        else handles.push_back(handle);
    }
    void addRelationRoot(std::coroutine_handle<> handle) noexcept {
        // 注意: 该函数需要手动保护
        assert(relationMap.count(handle) == 0);
        relationMap[handle] = {};
    }
    void addRelation(std::coroutine_handle<> thisHandle, std::coroutine_handle<> reliedHandle) noexcept {
        // 注意: 该函数需要手动保护
        relationMap.at(reliedHandle).insert(thisHandle);
    }
    size_t resolveRelation(std::coroutine_handle<> thisHandle) noexcept {
        // 注意: 该函数需要手动保护
        size_t ret = relationMap.at(thisHandle).size();
        for (std::coroutine_handle<> relatedHandle : relationMap.at(thisHandle)) enqueue(relatedHandle, true);
        relationMap.erase(thisHandle);
        return ret;
    }
    void shutdown() noexcept {
        if (stop) return;
        stop = true;
        condition.notify_all();
        for (auto& worker : workers) worker.join();
    }
    ~ThreadPool() noexcept { shutdown(); }
};

ThreadPool* _globalThreadPool;
void startThreadPool(int n = 64) { _globalThreadPool = new ThreadPool {n}; }
void stopThreadPool() { delete _globalThreadPool; }

// 协程promise
class PromiseInitialAwaitable {
public:
    PromiseInitialAwaitable() noexcept {}
    PromiseInitialAwaitable(const PromiseInitialAwaitable&) = delete;
    PromiseInitialAwaitable& operator= (const PromiseInitialAwaitable&) = delete;
    PromiseInitialAwaitable(PromiseInitialAwaitable&&) = delete;
    PromiseInitialAwaitable& operator= (PromiseInitialAwaitable&&) = delete;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        _globalThreadPool->acquireLock();
        _globalThreadPool->enqueue(handle);
        _globalThreadPool->addRelationRoot(handle);
        _globalThreadPool->releaseLock();
        _globalThreadPool->notify_n();
    }
    void await_resume() const noexcept {}
};

template <typename Promise_T, typename Async_T>
class PromiseBase {
protected:
    bool ready;
    std::coroutine_handle<Promise_T> handle;
public:
    PromiseBase() noexcept : ready(false), handle(std::coroutine_handle<Promise_T>::from_promise(*static_cast<Promise_T*>(this))) {}
    Async_T get_return_object() const noexcept { return handle; }
    PromiseInitialAwaitable initial_suspend() const noexcept { return {}; }
    std::suspend_always final_suspend() const noexcept { return {}; }
    void unhandled_exception() const noexcept {}
    bool isReady() const noexcept { return ready; }
};

template <typename Async_T, typename Return_T, bool IsMove = false, typename _Enabled = void>
class Promise : public PromiseBase<Promise<Async_T, Return_T>, Async_T> {
private:
    using ThisType = Promise<Async_T, Return_T, IsMove, _Enabled>;
    std::optional<Return_T> value;
public:
    Promise() noexcept : PromiseBase<ThisType, Async_T>(), value(std::nullopt) {}
    ~Promise() noexcept { this->handle.destroy(); }
    void return_value(Return_T value_) noexcept {
        _globalThreadPool->acquireLock();
        this->ready = true;
        value = value_;
        size_t n = _globalThreadPool->resolveRelation(this->handle);
        _globalThreadPool->releaseLock();
        _globalThreadPool->notify_n(n);
    }
    Return_T getValue() const noexcept { return *value; }
};

template <typename Async_T, typename Return_T>
class Promise<Async_T, Return_T, false, std::enable_if_t<std::is_reference_v<Return_T>>> : public PromiseBase<Promise<Async_T, Return_T>, Async_T> {
private:
    using ThisType = Promise<Async_T, Return_T>;
    std::remove_reference_t<Return_T>* value;
public:
    Promise() noexcept : PromiseBase<ThisType, Async_T>(), value(nullptr) {}
    ~Promise() noexcept { this->handle.destroy(); }
    void return_value(Return_T value_) noexcept {
        _globalThreadPool->acquireLock();
        this->ready = true;
        value = &value_;
        size_t n = _globalThreadPool->resolveRelation(this->handle);
        _globalThreadPool->releaseLock();
        _globalThreadPool->notify_n(n);
    }
    Return_T getValue() noexcept { return static_cast<Return_T>(*value); }
};

template <typename Async_T>
class Promise<Async_T, void> : public PromiseBase<Promise<Async_T, void>, Async_T> {
private:
    using ThisType = Promise<Async_T, void>;
public:
    Promise() noexcept : PromiseBase<ThisType, Async_T>() {}
    ~Promise() noexcept { this->handle.destroy(); }
    void return_void() noexcept {
        _globalThreadPool->acquireLock();
        this->ready = true;
        size_t n = _globalThreadPool->resolveRelation(this->handle);
        _globalThreadPool->releaseLock();
        _globalThreadPool->notify_n(n);
    }
};

template <typename Async_T, typename Return_T>
class Promise<Async_T, Return_T, true> : public PromiseBase<Promise<Async_T, Return_T, true>, Async_T> {
    static_assert(!std::is_reference_v<Return_T> && !std::is_void_v<Return_T>);
private:
    using ThisType = Promise<Async_T, Return_T, true>;
    std::optional<Return_T> value;
public:
    Promise() noexcept : PromiseBase<ThisType, Async_T>(), value(std::nullopt) {}
    ~Promise() noexcept { this->handle.destroy(); }
    void return_value(Return_T value_) noexcept {
        _globalThreadPool->acquireLock();
        this->ready = true;
        value = value_;
        size_t n = _globalThreadPool->resolveRelation(this->handle);
        _globalThreadPool->releaseLock();
        _globalThreadPool->notify_n(n);
    }
    Return_T&& getValue() noexcept { return std::move(*value); }
};

// 协程等待对象
template <typename Awaitable_T, typename Async_T>
class AwaitableBase {
protected:
    std::coroutine_handle<typename Async_T::promise_type> reliedHandle;
public:
    AwaitableBase(const Async_T& task_) noexcept : reliedHandle(task_.handle) {}
    AwaitableBase(const AwaitableBase&) = delete;
    AwaitableBase& operator= (const AwaitableBase&) = delete;
    AwaitableBase(AwaitableBase&&) = delete;
    AwaitableBase& operator= (AwaitableBase&&) = delete;
    bool await_ready() const noexcept {
        _globalThreadPool->acquireLock();
        bool ret = reliedHandle.promise().isReady();
        if (ret) _globalThreadPool->releaseLock();
        return ret;
    }
    void await_suspend(std::coroutine_handle<> handle) const noexcept {
        _globalThreadPool->addRelation(handle, reliedHandle);
        _globalThreadPool->releaseLock();
    }
};

template <typename Async_T, typename Return_T, bool IsMove = false, typename _Enabled = void>
class Awaitable : public AwaitableBase<Awaitable<Async_T, Return_T, IsMove>, Async_T> {
private:
    using ThisType = Awaitable<Async_T, Return_T, IsMove, _Enabled>;
public:
    Awaitable(const Async_T& task_) noexcept : AwaitableBase<ThisType, Async_T>(task_) {}
    Return_T await_resume() const noexcept { return this->reliedHandle.promise().getValue(); }
};

template <typename Async_T, typename Return_T>
class Awaitable<Async_T, Return_T, false, std::enable_if_t<std::is_reference_v<Return_T>>> : public AwaitableBase<Awaitable<Async_T, Return_T>, Async_T> {
private:
    using ThisType = Awaitable<Async_T, Return_T>;
public:
    Awaitable(const Async_T& task_) noexcept : AwaitableBase<ThisType, Async_T>(task_) {}
    Return_T await_resume() noexcept { return this->reliedHandle.promise().getValue(); }
};

template <typename Async_T>
class Awaitable<Async_T, void> : public AwaitableBase<Awaitable<Async_T, void>, Async_T> {
private:
    using ThisType = Awaitable<Async_T, void>;
public:
    Awaitable(const Async_T& task_) noexcept : AwaitableBase<ThisType, Async_T>(task_) {}
    void await_resume() const noexcept {}
};

template <typename Async_T, typename Return_T>
class Awaitable<Async_T, Return_T, true> : public AwaitableBase<Awaitable<Async_T, Return_T, true>, Async_T> {
    static_assert(!std::is_reference_v<Return_T> && !std::is_void_v<Return_T>);
private:
    using ThisType = Awaitable<Async_T, Return_T, true>;
public:
    Awaitable(const Async_T& task_) noexcept : AwaitableBase<ThisType, Async_T>(task_) {}
    Return_T&& await_resume() noexcept { return this->reliedHandle.promise().getValue(); }
};

// 协程返回类型
template <typename Return_T = void, bool IsMove = false>
class Async {
private:
    using ThisType = Async<Return_T, IsMove>;
    friend class AwaitableBase<Awaitable<ThisType, Return_T, IsMove>, ThisType>;
public:
    using promise_type = Promise<ThisType, Return_T, IsMove>;
private:
    std::coroutine_handle<promise_type> handle;
public:
    Async() noexcept : handle() {}
    Async(std::coroutine_handle<promise_type> handle_) noexcept : handle(handle_) {}
    Async(const Async&) = delete;
    Async& operator= (const Async&) = delete;
    Async(Async&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    Async& operator= (Async&& other) noexcept {
        assert(!handle);
        handle = std::exchange(other.handle, {});
        return *this;
    }
    Awaitable<ThisType, Return_T, IsMove> operator co_await() const noexcept {
        return *this;
    }
};

} // namespace (anonymous)

namespace co {

template <typename Return_T = void, bool IsMove = false>
using co_async = ::Async<Return_T, IsMove>;
using ::startThreadPool;
using ::stopThreadPool;

} // namespace co

#endif // __COROUTINE_HELPER_HPP__