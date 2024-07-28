# C++ Header-only 协程并发库
这只是一个极简的利用C++20的协程特性实现了并发的库. 语法上*模仿了python的async/await*. **不要指望一个不到300行代码的库有什么NB的功能.**
## 依赖项
编译时添加-std=c++20, 其余无.
## 使用方法
```c++
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

#include "coroutineHelper.hpp"
using co::co_async; // 否则需要co::co_async
__attribute__((constructor)) void _co_init() {co::startThreadPool(64);} // 也可以放在main()开始的位置. 初始化线程池.
__attribute__((destructor)) void _co_fin() {co::stopThreadPool();} // 也可以放在main()结束的位置. 销毁线程池.

co_async<int> func1() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    co_return 42;
}

co_async<int> func2() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    co_return 24;
}

std::promise<void> _p; // 同步用

co_async<> mainFunc() {
    auto task1 = func1();
    auto task2 = func2();
    printf("task1 : %d, task2 : %d\n", co_await task1, co_await task2);
    _p.set_value(); // 同步用
    co_return;
}

long long getTime() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

int main() {
    auto t = getTime();
    mainFunc();
    _p.get_future().wait();
    printf("time used: %lld ms.\n", getTime() - t);
}
```
其他使用方法可参考coroutineTest.cpp
## 参数
co::co_async接受两个模板参数
- Return_T: 该co_async的返回值类型. 默认为void.
- IsMove: 是否要移动该co_async的返回值. 默认为false(每次被co_await时将返回值复制一份, 或直接返回引用自身). 若该项为true, 则Return_T必须**不为void且不为引用**, 且该co_async只能被co_await一次.
## 特性
- 一个co_async可以**被多次co_await(除非IsMove为true)**, 每次被co_await时均复制一份co_async的返回值. 注意: 本库**不检查**引用的生命周期, 若co_async返回引用, 请确认引用有效! 若想从co_async返回大型对象, 推荐直接**返回该对象(而非引用), 并把IsMove设为true**, co_await时会移动该对象到目标值中.
- **不支持co_yield**. co_async可以被多次co_await的语义和co_yield冲突, 因而本库不支持co_yield. **后续会更新**每一个co_async的挂起点只能被co_await一次, 但支持co_yield语义的版本. (咕咕咕)

## 其他
觉得好用就给个star吧! 求求了. 有bug就提issue吧, 虽然我不一定会看.