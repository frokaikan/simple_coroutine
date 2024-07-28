#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <thread>
#include <vector>

#include "coroutineHelper.hpp"
using co::co_async;

long long getTime() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

int tmp;

co_async<int&> f() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    co_return tmp;
}

co_async<std::vector<int>, true> g() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    co_return {1, 2, 3, 4};
}

co_async<> h(FILE* fp) {
    fprintf(fp, ".");
    co_return;
}

std::promise<void> promise_f_g, promise_h;

co_async<> co_test_f_g() {
    tmp = 3;
    co_async<int&> f_res = f();
    co_async<std::vector<int>, true> g_res = g();
    // 多次co await
    printf("f : %d\n", co_await f_res);
    printf("f : %d\n", co_await f_res);
    co_await f_res = 14;
    printf("f : %d\n", co_await f_res);
    printf("f = %d, g[0] = %d\n", co_await f_res, (co_await g_res)[0]);
    // printf("bad : %d\n", (co_await g_res).at(0)); // 危险操作! g的返回值已经被移动给g_res
    promise_f_g.set_value();
    co_return;
}

co_async<> co_test_h() {
    std::vector<co_async<>> vec;
    FILE* fp = fopen("test.txt", "w");
    for (int i = 0; i < 1000000; ++i) {
        vec.push_back(h(fp));
    }
    for (auto& task : vec) {
        co_await task;
    }
    fclose(fp);
    promise_h.set_value();
    co_return;
}

int main() {
    co::startThreadPool(64);

    // f();
    // std::this_thread::sleep_for(std::chrono::seconds(3));
    printf("start test\n");

    long long t;
    t = getTime();
    co_test_f_g();
    promise_f_g.get_future().wait();
    printf("time used (f&g): %lld (ms)\n", getTime() - t);
    t = getTime();
    co_test_h();
    promise_h.get_future().wait();
    printf("time used (h * 1e6): %lld (ms)\n", getTime() - t);
    co::stopThreadPool();

    t = getTime();
    FILE* fp = fopen("test.txt", "w");
    std::vector<std::thread> pool;
    for (int i = 0; i < 100000; ++i) {
        if (pool.size() == 64) {
            pool.back().join();
            pool.pop_back();
        }
        pool.emplace_back([fp]() {
            fprintf(fp, ".");
        });
    }
    for (std::thread& t : pool) t.join();
    printf("time used (h * 1e5, thread): %lld (ms)\n", getTime() - t);
    fclose(fp);
}