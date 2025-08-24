#include <deque>

#include "Async/Async.h"

namespace clice::async {

/// The default event loop.
uv_loop_t* loop = nullptr;

namespace {

uv_loop_t instance;
uv_idle_t idle;
bool idle_running = false;
std::deque<promise_base*> tasks;

void each(uv_idle_t* idle) {
    if(idle_running && tasks.empty()) {
        idle_running = false;
        uv_check_result(uv_idle_stop(idle));
    }

    /// Resume may create new tasks, we want to run them in the next iteration.
    auto all = std::move(tasks);
    for(auto& task: all) {
        task->resume();
    }
}

}  // namespace

void promise_base::schedule() {
    if(loop && !idle_running && tasks.empty()) {
        idle_running = true;
        uv_check_result(uv_idle_start(&idle, each));
    }

    tasks.push_back(this);
}

void init() {
    loop = &instance;

    uv_check_result(uv_loop_init(loop));

    idle_running = true;
    uv_check_result(uv_idle_init(loop, &idle));
    uv_check_result(uv_idle_start(&idle, each));
}

void run() {
    if(!loop) {
        init();
    }

    unsigned thread_num = std::thread::hardware_concurrency();
    if(thread_num == 0) {
        // If hardware_concurrency is not available, set a default value.
        thread_num = 8;
    }

    char buffer[8] = {};
    std::format_to_n(buffer, sizeof(buffer), "{}", thread_num);
    uv_check_result(uv_os_setenv("UV_THREADPOOL_SIZE", buffer));

    uv_check_result(uv_run(loop, UV_RUN_DEFAULT));

    stop();

    /// Run agian to cleanup the loop.
    uv_check_result(uv_run(loop, UV_RUN_DEFAULT));
    uv_check_result(uv_loop_close(loop));

    /// Clear all unfinished tasks.
    for(auto task: tasks) {
        if(task->cancelled()) {
            task->resume();
        } else {
            task->destroy();
        }
    }

    loop = nullptr;
}

void stop() {
    auto walk_cb = [](uv_handle_s* handle, void* arg) {
        if(!uv_is_closing(handle)) {
            uv_close(handle, nullptr);
        }
    };

    /// Close all handles.
    uv_walk(async::loop, walk_cb, nullptr);
}

}  // namespace clice::async
