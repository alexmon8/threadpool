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

#include "threadpool/ThreadPool.hpp"  // For Priority enum

namespace threadpool {

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

    /**
     * @brief Cleanup completed tasks to prevent memory leak
     *
     * Removes tasks from all_tasks_ and dependency_graph_ when:
     * 1. Task is in COMPLETED state
     * 2. All dependent tasks have moved past WAITING state
     *
     * Must be called with task_mutex_ held.
     */
    void cleanup_completed_tasks();

    // Central task registry (all tasks: waiting, ready, executing, completed)
    std::unordered_map<TaskID, std::shared_ptr<TaskNode>> all_tasks_;

    // Persistent dependency graph for incremental cycle detection
    // Maps: TaskID -> vector of tasks that depend on it (forward edges)
    std::unordered_map<TaskID, std::vector<TaskID>> dependency_graph_;

    // Priority queue of tasks ready to execute (dependencies satisfied)
    std::priority_queue<std::shared_ptr<TaskNode>,
                       std::vector<std::shared_ptr<TaskNode>>,
                       TaskNodeComparator> ready_queue_;

    // Worker threads
    std::vector<std::thread> workers_;

    // Atomic counters for ID and sequence generation
    std::atomic<uint64_t> next_task_id_;
    std::atomic<uint64_t> task_sequence_;

    // Task cleanup tracking (to prevent memory leak)
    std::atomic<uint64_t> completed_task_count_;
    std::atomic<uint64_t> last_cleanup_at_;
    static constexpr size_t CLEANUP_THRESHOLD = 1000;  // Cleanup every N completions

    // Synchronization primitives (cache-aligned to prevent false sharing)
    alignas(64) mutable std::mutex task_mutex_;      // Protects all_tasks_
    alignas(64) mutable std::mutex ready_mutex_;     // Protects ready_queue_
    std::condition_variable ready_cv_;

    // Shutdown flag
    std::atomic<bool> stop_;
};

// ============================================================================
// Implementation
// ============================================================================

inline DependencyThreadPool::DependencyThreadPool(size_t num_threads)
    : next_task_id_(1), task_sequence_(0),
      completed_task_count_(0), last_cleanup_at_(0),
      stop_(false)
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

            // Check if this task ID was ever created
            if (dep.value >= next_task_id_.load()) {
                throw std::invalid_argument("Dependency task not found: " +
                                           std::to_string(dep.value));
            }

            auto it = all_tasks_.find(dep);
            // If task not found but ID is valid (< next_task_id_),
            // it was completed and cleaned up, so we can skip it
            if (it == all_tasks_.end()) {
                continue;  // Skip - task was completed and cleaned up
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

            // Clean up dependency graph entry for completed task
            // This keeps the graph sparse and improves cycle detection performance
            dependency_graph_.erase(node->id);

            // Also remove edges pointing to this completed task
            for (const auto& [task_id, edges] : dependency_graph_) {
                auto& edge_list = dependency_graph_[task_id];
                edge_list.erase(std::remove(edge_list.begin(), edge_list.end(), node->id),
                               edge_list.end());
            }

            // Periodic cleanup to prevent memory leak
            // Trigger cleanup every CLEANUP_THRESHOLD task completions
            uint64_t completions = completed_task_count_.fetch_add(1, std::memory_order_relaxed);
            if (completions - last_cleanup_at_.load(std::memory_order_relaxed) >= CLEANUP_THRESHOLD) {
                cleanup_completed_tasks();
            }
        }
    }
}

inline bool DependencyThreadPool::has_cycle(TaskID new_task_id,
                                            const std::unordered_set<TaskID>& dependencies) {
    // Must be called with task_mutex_ held
    //
    // Incremental cycle detection using persistent dependency_graph_
    // Complexity: O(k) where k = reachable nodes from new_task
    // (vs O(n²) for rebuilding entire graph)

    // Add forward edges to persistent graph: dep -> new_task_id
    for (TaskID dep : dependencies) {
        dependency_graph_[dep].push_back(new_task_id);
    }

    // Check if new_task_id can reach itself via its dependencies
    // (i.e., is there a path from any dependency back to new_task_id?)
    std::unordered_set<TaskID> visited;
    std::unordered_set<TaskID> recursion_stack;

    std::function<bool(TaskID)> dfs = [&](TaskID current) -> bool {
        if (recursion_stack.count(current)) {
            return true;  // Cycle detected (back edge)
        }

        if (visited.count(current)) {
            return false;  // Already explored this path
        }

        visited.insert(current);
        recursion_stack.insert(current);

        // Explore all outgoing edges (tasks that depend on current)
        auto it = dependency_graph_.find(current);
        if (it != dependency_graph_.end()) {
            for (TaskID next : it->second) {
                // Skip completed tasks (can't form cycles)
                auto task_it = all_tasks_.find(next);
                if (task_it == all_tasks_.end() ||
                    task_it->second->state.load() == TaskState::COMPLETED) {
                    continue;
                }

                if (dfs(next)) {
                    return true;
                }
            }
        }

        recursion_stack.erase(current);
        return false;
    };

    // Start DFS from new task
    bool has_cycle = dfs(new_task_id);

    if (has_cycle) {
        // Rollback edges we just added
        for (TaskID dep : dependencies) {
            auto& edges = dependency_graph_[dep];
            edges.erase(std::remove(edges.begin(), edges.end(), new_task_id),
                       edges.end());
        }
    }

    return has_cycle;
}

inline void DependencyThreadPool::cleanup_completed_tasks() {
    // Must be called with task_mutex_ held
    //
    // Strategy: Remove completed tasks that have no active dependents
    // A task can be safely removed when:
    // 1. It's in COMPLETED state
    // 2. All its dependents are either non-existent or past WAITING state
    //
    // This prevents unbounded growth of all_tasks_ in long-running applications

    std::vector<TaskID> to_remove;

    for (const auto& [id, node] : all_tasks_) {
        // Only consider completed tasks
        if (node->state.load() != TaskState::COMPLETED) {
            continue;
        }

        // Check if all dependents are safe to ignore
        bool safe_to_remove = true;
        for (TaskID dep_id : node->dependents) {
            auto it = all_tasks_.find(dep_id);
            // If dependent exists and is still waiting, can't remove yet
            if (it != all_tasks_.end() &&
                it->second->state.load() == TaskState::WAITING) {
                safe_to_remove = false;
                break;
            }
        }

        if (safe_to_remove) {
            to_remove.push_back(id);
        }
    }

    // Remove tasks and their graph entries
    for (TaskID id : to_remove) {
        all_tasks_.erase(id);
        dependency_graph_.erase(id);
    }

    // Update cleanup counter
    last_cleanup_at_.store(completed_task_count_.load());
}

} // namespace threadpool

#endif // DEPENDENCY_THREADPOOL_HPP
