#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace wannaviewer::test {

struct TestCase final { std::string name; std::function<void()> run; };
std::vector<TestCase>& Registry();

struct Registration final {
    Registration(std::string name, std::function<void()> run);
};

inline void Require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) throw std::runtime_error(std::string(file) + ':' + std::to_string(line) + " requirement failed: " + expression);
}

} // namespace wannaviewer::test

#define WV_JOIN_DETAIL(a, b) a##b
#define WV_JOIN(a, b) WV_JOIN_DETAIL(a, b)
#define WV_TEST(name) \
    static void WV_JOIN(TestFunction_, __LINE__)(); \
    static ::wannaviewer::test::Registration WV_JOIN(TestRegistration_, __LINE__)(name, WV_JOIN(TestFunction_, __LINE__)); \
    static void WV_JOIN(TestFunction_, __LINE__)()
#define WV_REQUIRE(expression) ::wannaviewer::test::Require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
