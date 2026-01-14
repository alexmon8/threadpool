#ifndef WORKSTEALING_THREADPOOL_HPP
#define WORKSTEALING_THREADPOOL_HPP

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <random>
#include <atomic>

namespace threadpool {

/**
 * @brief Work-stealing thread pool for load-balanced task execution
 *
 * Each worker thread maintains its own task queue. When a thread's queue is empty,
 * it attempts to "steal" tasks from other threads' queues, providing automatic
 * load balancing without global lock contention.
 *
 * Key features:
 * - Per-thread task queues minimize lock contention
 * - Work stealing provides automatic load balancing
 * - LIFO for local work (cache-friendly)
 * - FIFO for stolen work (breadth-first execution)
 *
 * Example usage:
 * @code
 *   WorkStealingThreadPool pool(4);
 *   auto future = pool.submit([]() { return compute(); });
 *   int result = future.get();
 * @endcode
 */
class WorkStealingThreadPool {
public:
    /**
     * @brief Construct work-stealing thread pool
     * @param num_threads Number of worker threads (default: hardware concurrency)
     * @throws std::invalid_argument if num_threads is 0
     */
    explicit WorkStealingThreadPool(size_t num_threads = std::thread::hardware_concurrency());

    /**
     * @brief Destructor - waits for all pending tasks to complete
     */
    ~WorkStealingThreadPool();

    // Delete copy and move operations
    WorkStealingThreadPool(const WorkStealingThreadPool&) = delete;
    WorkStealingThreadPool& operator=(const WorkStealingThreadPool&) = delete;
    WorkStealingThreadPool(WorkStealingThreadPool&&) = delete;
    WorkStealingThreadPool& operator=(WorkStealingThreadPool&&) = delete;

    /**
     * @brief Submit a task for asynchronous execution
     *
     * @tparam F Callable type (function, lambda, functor)
     * @tparam Args Argument types for the callable
     * @param f Callable object to execute
     * @param args Arguments to pass to the callable
     * @return std::future<ReturnType> Future for retrieving the result
     * @throws std::runtime_error if pool is stopped
     *
     * The task is added to the calling thread's queue if called from a worker,
     * otherwise it's added to a random worker's queue.
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Get the number of worker threads
     * @return Number of threads in the pool
     */
    size_t size() const noexcept { return workers_.size(); }

private:
    /**
     * @brief Per-thread work queue
     */
    struct WorkQueue {
        std::deque<std::function<void()>> tasks;
        mutable std::mutex mutex;

        // Try to pop a task from the bottom (LIFO - for owner thread)
        bool pop_bottom(std::function<void()>& task) {
            std::unique_lock<std::mutex> lock(mutex);
            if (tasks.empty()) {
                return false;
            }
            task = std::move(tasks.back());
            tasks.pop_back();
            return true;
        }

        // Try to steal a task from the top (FIFO - for thief threads)
        bool steal_top(std::function<void()>& task) {
            std::unique_lock<std::mutex> lock(mutex);
            if (tasks.empty()) {
                return false;
            }
            task = std::move(tasks.front());
            tasks.pop_front();
            return true;
        }

        // Push a task to the bottom
        void push_bottom(std::function<void()> task) {
            std::unique_lock<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        }

        // Check if queue is empty
        bool empty() const {
            std::unique_lock<std::mutex> lock(mutex);
            return tasks.empty();
        }
    };

    /**
     * @brief Worker thread main loop
     * @param worker_id ID of this worker thread
     */
    void worker_thread(size_t worker_id);

    /**
     * @brief Try to steal work from another thread
     * @param thief_id ID of the thread attempting to steal
     * @param task Output parameter for the stolen task
     * @return true if a task was stolen, false otherwise
     */
    bool try_steal(size_t thief_id, std::function<void()>& task);

    /**
     * @brief Get the worker ID for the current thread
     * @return Worker ID, or num_threads if not a worker thread
     */
    size_t get_worker_id() const;

    // Worker threads
    std::vector<std::thread> workers_;

    // Per-thread work queues
    std::vector<WorkQueue> queues_;

    // Thread ID to worker ID mapping
    std::vector<std::thread::id> thread_ids_;

    // Global condition variable for worker wake-up
    std::condition_variable work_available_;
    std::mutex global_mutex_;

    // Shutdown flag
    std::atomic<bool> stop_;

    // Random number generator for steal victim selection
    mutable std::mt19937 rng_;
};

// Implementation

inline WorkStealingThreadPool::WorkStealingThreadPool(size_t num_threads)
    : queues_(num_threads)
    , thread_ids_(num_threads)
    , stop_(false)
    , rng_(std::random_device{}())
{
    if (num_threads == 0) {
        throw std::invalid_argument("WorkStealingThreadPool size must be greater than 0");
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i] {
            thread_ids_[i] = std::this_thread::get_id();
            worker_thread(i);
        });
    }

    // Wait for all threads to initialize their IDs
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

inline WorkStealingThreadPool::~WorkStealingThreadPool() {
    stop_.store(true);
    work_available_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template<typename F, typename... Args>
auto WorkStealingThreadPool::submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    if (stop_.load()) {
        throw std::runtime_error("Cannot submit task to stopped WorkStealingThreadPool");
    }

    // Get worker ID for current thread
    size_t worker_id = get_worker_id();

    // If called from a worker thread, push to its own queue
    // Otherwise, push to a random queue
    if (worker_id < workers_.size()) {
        queues_[worker_id].push_bottom([task]() { (*task)(); });
    } else {
        // External submission - use random queue
        std::uniform_int_distribution<size_t> dist(0, workers_.size() - 1);
        size_t target = dist(rng_);
        queues_[target].push_bottom([task]() { (*task)(); });
    }

    work_available_.notify_one();
    return result;
}

inline size_t WorkStealingThreadPool::get_worker_id() const {
    auto tid = std::this_thread::get_id();
    for (size_t i = 0; i < thread_ids_.size(); ++i) {
        if (thread_ids_[i] == tid) {
            return i;
        }
    }
    return workers_.size();  // Not a worker thread
}

inline bool WorkStealingThreadPool::try_steal(size_t thief_id, std::function<void()>& task) {
    // Try to steal from a random victim
    size_t num_workers = workers_.size();

    // Randomly shuffle potential victims
    std::vector<size_t> victims;
    for (size_t i = 0; i < num_workers; ++i) {
        if (i != thief_id) {
            victims.push_back(i);
        }
    }
    std::shuffle(victims.begin(), victims.end(), rng_);

    // Try each victim in random order
    for (size_t victim : victims) {
        if (queues_[victim].steal_top(task)) {
            return true;
        }
    }

    return false;
}

inline void WorkStealingThreadPool::worker_thread(size_t worker_id) {
    while (true) {
        std::function<void()> task;

        // Try to get work from own queue (LIFO - cache friendly)
        if (queues_[worker_id].pop_bottom(task)) {
            task();
            continue;
        }

        // Own queue is empty, try to steal from others
        if (try_steal(worker_id, task)) {
            task();
            continue;
        }

        // No work available anywhere, check if we should exit
        if (stop_.load()) {
            return;
        }

        // Wait for new work
        {
            std::unique_lock<std::mutex> lock(global_mutex_);
            work_available_.wait_for(lock, std::chrono::milliseconds(10), [this, worker_id] {
                return stop_.load() || !queues_[worker_id].empty();
            });
        }

        // Check again if we should exit
        if (stop_.load()) {
            // Drain remaining tasks before exiting
            while (queues_[worker_id].pop_bottom(task)) {
                task();
            }
            return;
        }
    }
}

} // namespace threadpool

#endif // WORKSTEALING_THREADPOOL_HPP
