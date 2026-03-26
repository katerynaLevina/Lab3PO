#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

using namespace std;

//  вивід у консоль
mutex cout_mtx;
void print_log(const string& msg) {
    lock_guard<mutex> lock(cout_mtx);
    cout << msg << endl;
}

// cтруктура задачі
struct Task {
    int id;
    int duration_sec;
    function<void()> func;
    chrono::steady_clock::time_point enqueue_time;
};

class ThreadPool {
private:
    vector<thread> workers;
    thread manager;

    // gодвійна буферизація
    queue<Task> execute_queue;
    queue<Task> accumulate_queue;

    mutex mtx;
    condition_variable cv_worker;
    condition_variable cv_manager;

    bool stop_flag = false;
    bool immediate_stop = false;
    bool paused = false;


    int accumulate_time_sec = 0;


    int tasks_rejected = 0;
    int tasks_completed = 0;
    double total_wait_time = 0;
    int tracked_wait_tasks = 0;

    // відстеження стану переповненої черги
    bool is_full = false;
    chrono::steady_clock::time_point full_start_time;
    double max_full_time = -1.0;
    double min_full_time = 1e9;
    int full_periods_count = 0;

public:
    ThreadPool() {

        manager = thread([this]() {
            while (true) {
                unique_lock<mutex> lock(mtx);

                bool stopped = cv_manager.wait_for(lock, chrono::seconds(40), [this]() {
                    return stop_flag || immediate_stop;
                });

                if (immediate_stop || (stop_flag && accumulate_queue.empty() && execute_queue.empty())) {
                    break;
                }

                if (!stopped) {
                    print_log("[Manager] 40 seconds passed. Transferring tasks to execution.");

                    // фіксація статистики якщо чега переповнена
                    if (is_full) {
                        auto now = chrono::steady_clock::now();
                        double full_dur = chrono::duration<double>(now - full_start_time).count();
                        if (full_dur > max_full_time) max_full_time = full_dur;
                        if (full_dur < min_full_time) min_full_time = full_dur;
                        full_periods_count++;
                        is_full = false;
                    }

                    // переміщуємо задачі з буфера накопичення в буфер виконання
                    while (!accumulate_queue.empty()) {
                        execute_queue.push(move(accumulate_queue.front()));
                        accumulate_queue.pop();
                    }
                    accumulate_time_sec = 0;
                    cv_worker.notify_all();
                }
            }
        });

        // 4 робочих потоки
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([this, i]() {
                while (true) {
                    Task task;
                    {
                        unique_lock<mutex> lock(mtx);
                        cv_worker.wait(lock, [this]() {
                            return immediate_stop || (!execute_queue.empty() && !paused) || (stop_flag && execute_queue.empty());
                        });

                        if (immediate_stop) break;
                        if (stop_flag && execute_queue.empty()) break;
                        if (paused) continue;

                        task = move(execute_queue.front());
                        execute_queue.pop();

                        // підрахунок часу знаходження в стані очікування
                        auto now = chrono::steady_clock::now();
                        double wait_t = chrono::duration<double>(now - task.enqueue_time).count();
                        total_wait_time += wait_t;
                        tracked_wait_tasks++;
                    }

                    print_log("[Worker " + to_string(i) + "] Started task " + to_string(task.id) + " (" + to_string(task.duration_sec) + " sec)");
                    task.func();

                    lock_guard<mutex> lock(mtx);
                    tasks_completed++;
                    print_log("[Worker " + to_string(i) + "] Completed task " + to_string(task.id));
                }
            });
        }
    }

    ~ThreadPool() {
        shutdown(false);
    }

    int main() {

    }