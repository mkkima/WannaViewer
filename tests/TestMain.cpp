#include "TestHarness.hpp"

#include <exception>
#include <iostream>

namespace wannaviewer::test {

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registration::Registration(std::string name, std::function<void()> run) {
    Registry().push_back({std::move(name), std::move(run)});
}

} // namespace wannaviewer::test

int main() {
    unsigned failures = 0;
    for (const auto& test : wannaviewer::test::Registry()) {
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << (wannaviewer::test::Registry().size() - failures) << '/' << wannaviewer::test::Registry().size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
