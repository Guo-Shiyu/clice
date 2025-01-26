#include "Test/CTest.h"
#include "Feature/InlayHint.h"
#include "Basic/SourceConverter.h"

#include <latch>
#include <thread>

namespace clice::testing {

namespace {

struct ZMultiThread : public ::testing::Test {

    static size_t Concurrency;

    std::vector<Tester> txs;
    std::atomic<size_t> compileCompleteCount;
    std::latch startLatch;

    ZMultiThread() : txs(), compileCompleteCount(0), startLatch(Concurrency) {}

    void run(llvm::StringRef code) {
        txs.reserve(Concurrency);

        for(int i = 0; i < Concurrency; i++) {
            txs.emplace_back("main.cpp", code);
        }

        for(int i = 0; i < Concurrency; i++) {
            auto thread = std::thread([this, i]() {
                startLatch.arrive_and_wait();
                txs[i].run();
                compileCompleteCount += 1;
            });
            thread.detach();
        }

        while(compileCompleteCount < Concurrency) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }
    }
};

size_t ZMultiThread::Concurrency = std::thread::hardware_concurrency();

TEST_F(ZMultiThread, Example) {
    run(R"cpp(
struct S {
    int x = 0;
    int y = 0;
};

constexpr int f() {
    S s;
    return s.x + s.y;
}

#include <vector>
#include <format>

std::vector<int> vecs = {1, 2, 3};

std::string s = std::format("{}", 123);

)cpp");

    EXPECT_EQ(compileCompleteCount, Concurrency);

    for(int i = 0; i < Concurrency; i++) {
        EXPECT_TRUE(txs[i].info.has_value());
    }
}

}  // namespace
}  // namespace clice::testing
