/*
 * Copyright 2019-2023 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <util/task.h>
#include <queue>
#include <optional>
#include <cassert>

using namespace logid;
using namespace std::chrono;

// Priority ordering for the earliest scheduled task.
struct task_less {
private:
    std::greater<> greater;
public:
    bool operator()(const task& a, const task& b) const {
        return greater(a.time, b.time);
    }
};

static std::priority_queue<task, std::vector<task>, task_less> tasks {};
static std::mutex task_mutex {};
static std::condition_variable task_cv {};
static std::atomic_bool workers_init = false;
static std::atomic_bool workers_run = false;

// Signal all worker threads to stop and let them drain out.
void stop_workers() {
    std::unique_lock lock(task_mutex);
    if (workers_init) {
        workers_run = false;
        lock.unlock();
        task_cv.notify_all();

        /* Wait for all workers to end */
        lock.lock();
    }
}

// Worker loop that waits for the next due task and executes it off-thread.
void worker() {
    std::unique_lock lock(task_mutex);
    while (workers_run) {
        task_cv.wait(lock, []() { return !tasks.empty() || !workers_run; });

        if (!workers_run)
            break;

            // The next task is in the future, so sleep until it becomes due.
            if (tasks.top().time >= system_clock::now()) {
            auto wait = tasks.top().time - system_clock::now();
            task_cv.wait_for(lock, wait, []() {
                return (!tasks.empty() && (tasks.top().time < system_clock::now())) ||
                    !workers_run;
            });

            if (!workers_run)
                break;
        }

        if (!tasks.empty()) {
            // The queue may have changed while waiting, so re-check before running.
            auto f = tasks.top().function;
            tasks.pop();

            lock.unlock();
            try {
                f();
            } catch(std::exception& e) {
                ExceptionHandler::Default(e);
            }
            lock.lock();
        }
    }
}

// Spawn the background workers once during startup.
void logid::init_workers(int worker_count) {
    std::lock_guard lock(task_mutex);
    assert(!workers_init);

    for (int i = 0; i < worker_count; ++i)
        std::thread(&worker).detach();

    workers_init = true;
    workers_run = true;

    atexit(&stop_workers);
}

// Queue an immediate task.
void logid::run_task(std::function<void()> function) {
    task t{
            .function = std::move(function),
            .time = std::chrono::system_clock::now()
    };

    run_task(t);
}

// Queue a task to run after a delay.
void logid::run_task_after(std::function<void()> function, std::chrono::milliseconds delay) {
    task t{
            .function = std::move(function),
            .time = system_clock::now() + delay
    };

    run_task(t);
}

// Insert a task into the priority queue and wake one worker.
void logid::run_task(task t) {
    std::lock_guard lock(task_mutex);

    if (!workers_init) {
        throw std::runtime_error("tasks queued before work queue ready");
    }

    tasks.emplace(std::move(t));
    // TODO: only need to wake up at top
    task_cv.notify_one();
}
