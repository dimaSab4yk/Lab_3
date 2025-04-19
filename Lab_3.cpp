#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <random>
#include <atomic>

using namespace std;
using namespace std::chrono;

class Task 
{
public:
    Task(int id, int duration)
        : id(id), duration(duration), createdAt(steady_clock::now()) {}

    void operator()() const 
    {
        this_thread::sleep_for(seconds(duration));
    }

    int getDuration() const { return duration; }
    int getId() const { return id; }
    steady_clock::time_point getCreatedAt() const { return createdAt; }

private:
    int id;
    int duration;
    steady_clock::time_point createdAt;
};

struct TaskCompare 
{
    bool operator()(const Task& a, const Task& b)
    {
        return a.getDuration() > b.getDuration();
    }
};

class ThreadPool 
{
public:
    ThreadPool(size_t numThreads) : stop(false), paused(false),
        createdTasksTotal(0), executedTasks(0),
        totalWaitTime(0), totalExecTime(0), currentTotalDuration(0) 
    {
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.emplace_back(&ThreadPool::worker, this);
        }            
    }

    ~ThreadPool() 
    {
        shutdown(true);
    }

    bool addTask(Task task) 
    {
        unique_lock<mutex> lock(queueMutex);
        createdTasksTotal++;
        if (currentTotalDuration + task.getDuration() > MAX_TOTAL_DURATION) 
        {
            cout << "[Task " << task.getId() << "] Rejected (queue total > 50 sec)\n";
            return false;
        }
        taskQueue.push(task);
        currentTotalDuration += task.getDuration();
        condition.notify_one();
        return true;
    }

    void pause() 
    {
        paused = true;
    }

    void resume() 
    {
        paused = false;
        condition.notify_all();
    }

    void shutdown(bool waitForTasks) 
    {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();

        if (waitForTasks) 
        {
            for (thread& t : workers)
            {
                if (t.joinable()) t.join();
            }                
        }
        else 
        {
            for (thread& t : workers)
            {
                if (t.joinable()) t.detach();
            }                
        }

        cout << "\n--- Statistics ---\n";
        cout << "Total tasks created:   " << createdTasksTotal << "\n";
        cout << "Total tasks executed:  " << executedTasks << "\n";

        if (executedTasks > 0) {
            cout << "Average wait time:     " << (double)totalWaitTime / executedTasks << " sec\n";
            cout << "Average exec time:     " << (double)totalExecTime / executedTasks << " sec\n";
        }
    }

private:
    const int MAX_TOTAL_DURATION = 50;

    vector<thread> workers;
    priority_queue<Task, vector<Task>, TaskCompare> taskQueue;

    mutex queueMutex;
    condition_variable condition;

    atomic<bool> stop;
    atomic<bool> paused;

    int currentTotalDuration;

    atomic<int> createdTasksTotal;
    atomic<int> executedTasks;
    long long totalWaitTime;
    long long totalExecTime;

    void worker() 
    {
        while (true) 
        {
            Task task(0, 0);
            {
                unique_lock<mutex> lock(queueMutex);
                condition.wait(lock, [this] 
                    {
                    return stop || (!paused && !taskQueue.empty());
                    });

                if (stop && taskQueue.empty()) return;
                if (paused || taskQueue.empty()) continue;

                task = taskQueue.top();
                taskQueue.pop();
                currentTotalDuration -= task.getDuration();
            }

            auto now = steady_clock::now();
            auto waitTime = duration_cast<seconds>(now - task.getCreatedAt()).count();
            auto start = steady_clock::now();
            task();
            auto end = steady_clock::now();

            long long execTime = duration_cast<seconds>(end - start).count();

            {
                lock_guard<mutex> statLock(statMutex);
                executedTasks++;
                totalWaitTime += waitTime;
                totalExecTime += execTime;
            }

            cout << "[Task " << task.getId() << "] Done in " << execTime
                << " sec (Waited: " << waitTime << " sec)\n";
        }
    }

    mutex statMutex;
};

atomic<int> taskIdCounter(1);

void taskProducer(ThreadPool& pool) 
{
    mt19937 rng(random_device{}());
    uniform_int_distribution<int> dist(6, 12);

    for (int i = 0; i < 10; ++i) 
    {
        int duration = dist(rng);
        int id = taskIdCounter.fetch_add(1);  
        Task task(id, duration);
        pool.addTask(task);
        this_thread::sleep_for(milliseconds(500));
    }
}

int main() 
{
    ThreadPool pool(4);

    thread p1(taskProducer, ref(pool));
    thread p2(taskProducer, ref(pool));

    this_thread::sleep_for(seconds(10));
    cout << "\nPausing pool...\n";
    pool.pause();

    this_thread::sleep_for(seconds(5));
    cout << "Resuming pool...\n";
    pool.resume();

    p1.join();
    p2.join();

    this_thread::sleep_for(seconds(30));

    return 0;
}


