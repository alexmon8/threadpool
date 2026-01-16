#ifndef DEPENDENCY_THREADPOOL_HPP
#define DEPENDENCY_THREADPOOL_HPP

#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace threadpool {

/**
 * @brief Priority levels for task scheduling
 */
enum class Priority {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2
};

/**
 * @brief Strong type wrapper for task identifiers
 *
 * Provides type safety and prevents accidental mixing of task IDs with other integers.
 * Value of 0 is reserved for invalid/uninitialized task IDs.
 */
struct TaskID {
    uint64_t value;

    TaskID() : value(0) {}
    explicit TaskID(uint64_t v) : value(v) {}

    bool operator==(const TaskID& other) const { return value == other.value; }
    bool operator!=(const TaskID& other) const { return value != other.value; }
    bool operator<(const TaskID& other) const { return value < other.value; }

    bool is_valid() const { return value != 0; }
};

} // namespace threadpool

// Hash specialization for TaskID to enable use in unordered containers
namespace std {
    template<>
    struct hash<threadpool::TaskID> {
        size_t operator()(const threadpool::TaskID& id) const {
            return std::hash<uint64_t>{}(id.value);
        }
    };
}

namespace threadpool {

/**
 * @brief Thread pool with task dependency management and priority scheduling
 *
 * DependencyThreadPool allows tasks to specify dependencies on other tasks,
 * forming a directed acyclic graph (DAG). Tasks execute only when all their
 * dependencies are satisfied. Among ready tasks, higher priority tasks execute first.
 *
 * Features:
 * - DAG execution with automatic dependency resolution
 * - Priority scheduling (HIGH, MEDIUM, LOW) among ready tasks
 * - Circular dependency detection at submission time
 * - Thread-safe concurrent task submission
 * - Future-based result retrieval
 *
 * Example usage:
 * @code
 *   DependencyThreadPool pool(4);
 *
 *   auto [id1, f1] = pool.submit(Priority::HIGH, []() { return 42; });
 *   auto [id2, f2] = pool.submit(Priority::MEDIUM, {id1}, [](int x) {
 *       return x * 2;
 *   }, f1.get());
 *
 *   std::cout << f2.get() << std::endl;  // 84
 * @endcode
 */
class DependencyThreadPool {
public:
    /**
     * @brief Construct thread pool with specified number of workers
     * @param num_threads Number of worker threads (default: hardware concurrency)
     * @throws std::invalid_argument if num_threads is 0
     */
    explicit DependencyThreadPool(size_t num_threads = std::thread::hardware_concurrency());

    /**
     * @brief Destructor - waits for all tasks to complete and stops workers
     */
    ~DependencyThreadPool();

    // Delete copy and move operations
    DependencyThreadPool(const DependencyThreadPool&) = delete;
    DependencyThreadPool& operator=(const DependencyThreadPool&) = delete;
    DependencyThreadPool(DependencyThreadPool&&) = delete;
    DependencyThreadPool& operator=(DependencyThreadPool&&) = delete;

    /**
     * @brief Submit task without dependencies
     * @param priority Task priority (HIGH, MEDIUM, or LOW)
     * @param f Callable object (function, lambda, functor)
     * @param args Arguments to pass to the callable
     * @return Pair of TaskID and future for retrieving the result
     * @throws std::runtime_error if pool is stopped
     */
    template<typename F, typename... Args>
    std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
    submit(Priority priority, F&& f, Args&&... args);

    /**
     * @brief Submit task with dependencies
     * @param priority Task priority (HIGH, MEDIUM, or LOW)
     * @param dependencies Vector of TaskIDs this task depends on
     * @param f Callable object (function, lambda, functor)
     * @param args Arguments to pass to the callable
     * @return Pair of TaskID and future for retrieving the result
     * @throws std::invalid_argument if dependency task ID is not found
     * @throws std::runtime_error if circular dependency detected or pool is stopped
     */
    template<typename F, typename... Args>
    std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
    submit(Priority priority, const std::vector<TaskID>& dependencies, F&& f, Args&&... args);

    /**
     * @brief Submit task without dependencies (default MEDIUM priority)
     */
    template<typename F, typename... Args>
    std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
    submit(F&& f, Args&&... args);

    /**
     * @brief Submit task with dependencies (default MEDIUM priority)
     */
    template<typename F, typename... Args>
    std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
    submit(const std::vector<TaskID>& dependencies, F&& f, Args&&... args);

    /**
     * @brief Get number of worker threads
     */
    size_t size() const noexcept { return workers_.size(); }

    /**
     * @brief Check if a task has completed
     * @param id TaskID to check
     * @return true if task is completed, false otherwise
     */
    bool is_task_completed(TaskID id) const;

    /**
     * @brief Wait for a specific task to complete
     * @param id TaskID to wait for
     */
    void wait_for_task(TaskID id);

private:
    /**
     * @brief Task execution states
     */
    enum class TaskState {
        WAITING,    // Waiting for dependencies
        READY,      // Dependencies satisfied, ready to execute
        EXECUTING,  // Currently executing
        COMPLETED   // Execution finished
    };

    /**
     * @brief Node in the task dependency graph
     */
    struct TaskNode {
        TaskID id;
        Priority priority;
        uint64_t sequence;  // For FIFO ordering within same priority
        std::function<void()> task;

        // Dependency tracking
        std::unordered_set<TaskID> dependencies;        // Tasks this depends on
        std::atomic<size_t> remaining_dependencies;     // Count of unfinished dependencies
        std::unordered_set<TaskID> dependents;         // Tasks that depend on this

        // State
        std::atomic<TaskState> state;

        TaskNode(TaskID id, Priority p, uint64_t seq, std::function<void()> t,
                 std::unordered_set<TaskID> deps)
            : id(id), priority(p), sequence(seq), task(std::move(t)),
              dependencies(std::move(deps)),
              remaining_dependencies(dependencies.size()),
              state(dependencies.empty() ? TaskState::READY : TaskState::WAITING)
        {}
    };

    /**
     * @brief Comparator for priority queue (higher priority and earlier sequence first)
     */
    struct TaskNodeComparator {
        bool operator()(const std::shared_ptr<TaskNode>& a,
                       const std::shared_ptr<TaskNode>& b) const {
            if (a->priority != b->priority) {
                return a->priority < b->priority;  // Higher priority values come first
            }
            return a->sequence > b->sequence;  // Earlier sequences come first (FIFO)
        }
    };

    /**
     * @brief Worker thread main loop
     */
    void worker_thread();

    /**
     * @brief Detect circular dependencies using DFS
     * @param new_task_id TaskID of the new task being submitted
     * @param dependencies Dependencies of the new task
     * @return true if adding this task would create a cycle
     */
    bool has_cycle(TaskID new_task_id, const std::unordered_set<TaskID>& dependencies);

    // Central task registry (all tasks: waiting, ready, executing, completed)
    std::unordered_map<TaskID, std::shared_ptr<TaskNode>> all_tasks_;

    // Priority queue of tasks ready to execute (dependencies satisfied)
    std::priority_queue<std::shared_ptr<TaskNode>,
                       std::vector<std::shared_ptr<TaskNode>>,
                       TaskNodeComparator> ready_queue_;

    // Worker threads
    std::vector<std::thread> workers_;

    // Atomic counters for ID and sequence generation
    std::atomic<uint64_t> next_task_id_;
    std::atomic<uint64_t> task_sequence_;

    // Synchronization primitives
    mutable std::mutex task_mutex_;      // Protects all_tasks_
    mutable std::mutex ready_mutex_;     // Protects ready_queue_
    std::condition_variable ready_cv_;

    // Shutdown flag
    std::atomic<bool> stop_;
};

// ============================================================================
// Implementation
// ============================================================================

inline DependencyThreadPool::DependencyThreadPool(size_t num_threads)
    : next_task_id_(1), task_sequence_(0), stop_(false)
{
    if (num_threads == 0) {
        throw std::invalid_argument("DependencyThreadPool size must be greater than 0");
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&DependencyThreadPool::worker_thread, this);
    }
}

inline DependencyThreadPool::~DependencyThreadPool() {
    {
        std::unique_lock<std::mutex> lock(ready_mutex_);
        stop_ = true;
    }
    ready_cv_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template<typename F, typename... Args>
std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
DependencyThreadPool::submit(Priority priority, F&& f, Args&&... args) {
    // Submit without dependencies (empty vector)
    return submit(priority, std::vector<TaskID>{}, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
DependencyThreadPool::submit(F&& f, Args&&... args) {
    return submit(Priority::MEDIUM, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
DependencyThreadPool::submit(const std::vector<TaskID>& dependencies, F&& f, Args&&... args) {
    return submit(Priority::MEDIUM, dependencies, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
std::pair<TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
DependencyThreadPool::submit(Priority priority, const std::vector<TaskID>& dependencies,
                             F&& f, Args&&... args) {
    using return_type = typename std::invoke_result<F, Args...>::type;

    // Create packaged_task (same pattern as ThreadPool)
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<return_type> future = task->get_future();

    // Generate TaskID
    TaskID id(next_task_id_++);

    // Validate and normalize dependencies
    std::unordered_set<TaskID> dep_set;
    {
        std::unique_lock<std::mutex> lock(task_mutex_);

        if (stop_) {
            throw std::runtime_error("Cannot submit task to stopped DependencyThreadPool");
        }

        // Validate dependencies and filter out completed tasks
        for (TaskID dep : dependencies) {
            if (!dep.is_valid()) continue;

            auto it = all_tasks_.find(dep);
            if (it == all_tasks_.end()) {
                throw std::invalid_argument("Dependency task not found: " +
                                           std::to_string(dep.value));
            }

            // Only depend on non-completed tasks
            if (it->second->state.load() != TaskState::COMPLETED) {
                dep_set.insert(dep);
            }
        }

        // Detect circular dependencies
        if (!dep_set.empty() && has_cycle(id, dep_set)) {
            throw std::runtime_error("Circular dependency detected for task " +
                                    std::to_string(id.value));
        }

        // Create TaskNode
        auto node = std::make_shared<TaskNode>(
            id,
            priority,
            task_sequence_++,
            [task]() { (*task)(); },
            dep_set
        );

        // Register as dependent in parent tasks
        for (TaskID dep : dep_set) {
            all_tasks_[dep]->dependents.insert(id);
        }

        // Add to all_tasks_
        all_tasks_[id] = node;

        // If no dependencies, add to ready queue
        if (dep_set.empty()) {
            std::unique_lock<std::mutex> ready_lock(ready_mutex_);
            ready_queue_.push(node);
            ready_cv_.notify_one();
        }
    }

    return {id, std::move(future)};
}

inline bool DependencyThreadPool::is_task_completed(TaskID id) const {
    std::unique_lock<std::mutex> lock(task_mutex_);
    auto it = all_tasks_.find(id);
    if (it == all_tasks_.end()) return false;
    return it->second->state.load() == TaskState::COMPLETED;
}

inline void DependencyThreadPool::wait_for_task(TaskID id) {
    while (!is_task_completed(id)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

inline void DependencyThreadPool::worker_thread() {
    while (true) {
        std::shared_ptr<TaskNode> node;

        // Get a ready task
        {
            std::unique_lock<std::mutex> lock(ready_mutex_);
            ready_cv_.wait(lock, [this] {
                return stop_ || !ready_queue_.empty();
            });

            if (stop_ && ready_queue_.empty()) {
                return;
            }

            node = ready_queue_.top();
            ready_queue_.pop();
        }

        // Mark as executing
        node->state.store(TaskState::EXECUTING);

        // Execute task (outside locks)
        node->task();

        // Mark completed and notify dependents
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            node->state.store(TaskState::COMPLETED);

            // Notify all dependent tasks
            for (TaskID dependent_id : node->dependents) {
                auto it = all_tasks_.find(dependent_id);
                if (it == all_tasks_.end()) continue;

                auto& dependent = it->second;

                // Decrement dependency counter
                size_t remaining = --dependent->remaining_dependencies;

                // If all dependencies satisfied, move to ready queue
                if (remaining == 0) {
                    dependent->state.store(TaskState::READY);

                    std::unique_lock<std::mutex> ready_lock(ready_mutex_);
                    ready_queue_.push(dependent);
                    ready_cv_.notify_one();
                }
            }
        }
    }
}

inline bool DependencyThreadPool::has_cycle(TaskID new_task_id,
                                            const std::unordered_set<TaskID>& dependencies) {
    // Must be called with task_mutex_ held

    // Build temporary graph including the new task
    std::unordered_map<TaskID, std::vector<TaskID>> graph;

    // Add all existing task edges (exclude completed tasks)
    for (const auto& [tid, node] : all_tasks_) {
        if (node->state.load() == TaskState::COMPLETED) continue;

        for (TaskID dep : node->dependencies) {
            auto dep_it = all_tasks_.find(dep);
            if (dep_it != all_tasks_.end() &&
                dep_it->second->state.load() != TaskState::COMPLETED) {
                graph[dep].push_back(tid);  // dep -> tid edge
            }
        }
    }

    // Add edges for new task
    for (TaskID dep : dependencies) {
        graph[dep].push_back(new_task_id);
    }

    // DFS cycle detection using three colors
    enum Color { WHITE, GRAY, BLACK };
    std::unordered_map<TaskID, Color> colors;

    std::function<bool(TaskID)> dfs = [&](TaskID u) -> bool {
        colors[u] = GRAY;

        if (graph.find(u) != graph.end()) {
            for (TaskID v : graph[u]) {
                if (colors[v] == GRAY) {
                    return true;  // Back edge = cycle
                }
                if (colors[v] == WHITE && dfs(v)) {
                    return true;
                }
            }
        }

        colors[u] = BLACK;
        return false;
    };

    // Start DFS from new task
    return dfs(new_task_id);
}

} // namespace threadpool

#endif // DEPENDENCY_THREADPOOL_HPP
