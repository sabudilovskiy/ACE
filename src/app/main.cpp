#include <iostream>
#include <pool.hpp>
#include <fmt/format.h>
#include <fstream>
#include <sstream>




Task<int> foo() {
    //co_await jump on (E)
    co_return 1;
}

 Task<int> koo() {
    auto t1 = foo();
    auto t2 = foo();
    co_return co_await t1 + co_await t2;
}

int main() {
    engine::pool pool;
    pool.Shedule([]() {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        auto str = std::move(ss).str();
        str.append(".txt");
        std::ofstream file(str);
        file << "Ну провер очка";
    });
    pool.Shedule([]() {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        auto str = std::move(ss).str();
        str.append(".txt");
        std::ofstream file(str);
        file << "Ну провер очка";
    });
    std::this_thread::sleep_for(std::chrono::seconds(5));
    // auto task = koo();
    // task.handle_.promise().waiter = std::noop_coroutine();
    // task.handle_.resume();
    // auto res = task.handle_.promise().extract();
    // fmt::print("res: {}", res);
}
