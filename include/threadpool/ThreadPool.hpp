#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <atomic>

namespace threadpool {

/**
 * @brief Task priority levels
 */
enum class Priority {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2
};

/**
 * @brief A fixed-size thread pool for concurrent task execution
 *
 * This thread pool maintains a fixed number of worker threads that process
 * tasks from a shared priority queue. Tasks are executed by priority (HIGH > MEDIUM > LOW),
 * with FIFO order within the same priority level.
 *
 * Features:
 * - Submit tasks with automatic return value handling via std::future
 * - Priority-based task scheduling (HIGH, MEDIUM, LOW)
 * - Graceful shutdown with pending task completion
 * - Exception safety and resource cleanup (RAII)
 *
 * Example usage:
 * @code
 *   ThreadPool pool(4);  // Create pool with 4 threads
 *   auto future = pool.submit([]() { return 42; });  // Medium priority (default)
 *   auto high = pool.submit(Priority::HIGH, []() { return 99; });  // High priority
 *   int result = future.get();  // Block until task completes
 * @endcode
 */
class ThreadPool {
public:
    /**
     * @brief Construct thread pool with specified number of worker threads
     * @param num_threads Number of worker threads (default: hardware concurrency)
     * @throws std::invalid_argument if num_threads is 0
     */
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());

    /**
     * @brief Destructor - waits for all pending tasks to complete
     */
    ~ThreadPool();

    // Delete copy and move operations (thread pool is not copyable/movable)
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Submit a task for asynchronous execution with default priority
     *
     * @tparam F Callable type (function, lambda, functor)
     * @tparam Args Argument types for the callable
     * @param f Callable object to execute
     * @param args Arguments to pass to the callable
     * @return std::future<ReturnType> Future for retrieving the result
     * @throws std::runtime_error if pool is stopped
     *
     * The task will be queued with MEDIUM priority and executed by the next available worker thread.
     * The returned future can be used to retrieve the result or catch exceptions.
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Submit a task for asynchronous execution with specified priority
     *
     * @tparam F Callable type (function, lambda, functor)
     * @tparam Args Argument types for the callable
     * @param priority Task priority (HIGH, MEDIUM, or LOW)
     * @param f Callable object to execute
     * @param args Arguments to pass to the callable
     * @return std::future<ReturnType> Future for retrieving the result
     * @throws std::runtime_error if pool is stopped
     *
     * Higher priority tasks are executed before lower priority tasks.
     * Tasks with the same priority are executed in FIFO order.
     */
    template<typename F, typename... Args>
    auto submit(Priority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Get the number of worker threads
     * @return Number of threads in the pool
     */
    size_t size() const noexcept { return workers_.size(); }

    /**
     * @brief Check if the pool is stopped
     * @return true if pool is shut down, false otherwise
     */
    bool is_stopped() const noexcept;

private:
    /**
     * @brief Internal task wrapper with priority and sequence number
     */
    struct PriorityTask {
        Priority priority;
        uint64_t sequence;  // For FIFO ordering within same priority
        std::function<void()> task;

        // Comparison operator for priority queue (higher priority first)
        bool operator<(const PriorityTask& other) const {
            if (priority != other.priority) {
                return priority < other.priority;  // Higher priority values come first
            }
            return sequence > other.sequence;  // Earlier sequences come first (FIFO)
        }
    };

    /**
     * @brief Worker thread main loop
     *
     * Each worker continuously:
     * 1. Waits for a task or stop signal
     * 2. Executes the task
     * 3. Repeats until pool is stopped
     */
    void worker_thread();

    // Thread pool workers
    std::vector<std::thread> workers_;

    // Task priority queue
    std::priority_queue<PriorityTask> tasks_;

    // Synchronization primitives
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;

    // Task sequence counter for FIFO within priority
    std::atomic<uint64_t> task_sequence_;

    // Shutdown flag
    bool stop_;
};

// Implementation (header-only for template support)

inline ThreadPool::ThreadPool(size_t num_threads)
    : task_sequence_(0), stop_(false)
{
    if (num_threads == 0) {
        throw std::invalid_argument("ThreadPool size must be greater than 0");
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_thread(); });
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    return submit(Priority::MEDIUM, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPool::submit(Priority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    // Create packaged_task for the callable
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (stop_) {
            throw std::runtime_error("Cannot submit task to stopped ThreadPool");
        }

        // Create priority task with sequence number
        PriorityTask ptask{
            priority,
            task_sequence_++,
            [task]() { (*task)(); }
        };

        // Add to priority queue
        tasks_.push(std::move(ptask));
    }

    condition_.notify_one();
    return result;
}

inline bool ThreadPool::is_stopped() const noexcept {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return stop_;
}

inline void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Wait for a task or stop signal
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            // Exit if stopped and no pending tasks
            if (stop_ && tasks_.empty()) {
                return;
            }

            // Get highest priority task
            task = std::move(tasks_.top().task);
            tasks_.pop();
        }

        // Execute task (outside of lock)
        task();
    }
}

} // namespace threadpool

#endif // THREADPOOL_HPP
