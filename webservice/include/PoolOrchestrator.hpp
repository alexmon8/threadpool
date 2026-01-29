#ifndef POOLORCHESTRATOR_HPP
#define POOLORCHESTRATOR_HPP

#include <memory>
#include <string>
#include <future>
#include <vector>

#include "threadpool/ThreadPool.hpp"
#include "threadpool/WorkStealingThreadPool.hpp"
#include "threadpool/DependencyThreadPool.hpp"

namespace webservice {

/**
 * @brief Pool types available for task execution
 */
enum class PoolType {
    PRIORITY,      // ThreadPool - priority-based scheduling
    WORKSTEALING,  // WorkStealingThreadPool - load balancing
    DEPENDENCY     // DependencyThreadPool - DAG execution
};

/**
 * @brief Orchestrates task submission across multiple thread pool strategies
 *
 * Manages three different thread pool implementations and provides a unified
 * interface for submitting tasks. Automatically selects the optimal pool
 * based on operation characteristics.
 *
 * Pool Selection Strategy:
 * - Single operations → Priority pool (simple, priority-based)
 * - Batch operations → WorkStealing pool (automatic load balancing)
 * - Pipeline operations → Dependency pool (DAG execution)
 */
class PoolOrchestrator {
public:
    /**
     * @brief Construct orchestrator with specified thread count
     * @param num_threads Number of worker threads for each pool
     */
    explicit PoolOrchestrator(size_t num_threads);

    /**
     * @brief Destructor - ensures clean shutdown of all pools
     */
    ~PoolOrchestrator();

    // Delete copy and move
    PoolOrchestrator(const PoolOrchestrator&) = delete;
    PoolOrchestrator& operator=(const PoolOrchestrator&) = delete;
    PoolOrchestrator(PoolOrchestrator&&) = delete;
    PoolOrchestrator& operator=(PoolOrchestrator&&) = delete;

    /**
     * @brief Select optimal pool for operation type
     * @param operation Operation name ("resize", "batch", "pipeline", etc.)
     * @param batch_size Number of items to process (default 1)
     * @return Recommended pool type
     */
    PoolType select_pool_for_operation(const std::string& operation, size_t batch_size = 1) const;

    /**
     * @brief Submit task to priority pool
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param f Callable object
     * @param args Arguments to pass to callable
     * @return Future for retrieving result
     */
    template<typename F, typename... Args>
    auto submit_to_priority(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Submit task to priority pool with explicit priority
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param priority Task priority (HIGH, MEDIUM, LOW)
     * @param f Callable object
     * @param args Arguments to pass to callable
     * @return Future for retrieving result
     */
    template<typename F, typename... Args>
    auto submit_to_priority(threadpool::Priority priority, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Submit task to work-stealing pool
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param f Callable object
     * @param args Arguments to pass to callable
     * @return Future for retrieving result
     */
    template<typename F, typename... Args>
    auto submit_to_workstealing(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    /**
     * @brief Submit task to dependency pool without dependencies
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param priority Task priority
     * @param f Callable object
     * @param args Arguments to pass to callable
     * @return Pair of TaskID and future
     */
    template<typename F, typename... Args>
    auto submit_to_dependency(threadpool::Priority priority, F&& f, Args&&... args)
        -> std::pair<threadpool::TaskID, std::future<typename std::invoke_result<F, Args...>::type>>;

    /**
     * @brief Submit task to dependency pool with dependencies
     * @tparam F Callable type
     * @tparam Args Argument types
     * @param priority Task priority
     * @param dependencies Vector of task IDs this task depends on
     * @param f Callable object
     * @param args Arguments to pass to callable
     * @return Pair of TaskID and future
     */
    template<typename F, typename... Args>
    auto submit_to_dependency(threadpool::Priority priority,
                              const std::vector<threadpool::TaskID>& dependencies,
                              F&& f, Args&&... args)
        -> std::pair<threadpool::TaskID, std::future<typename std::invoke_result<F, Args...>::type>>;

    /**
     * @brief Get number of worker threads per pool
     */
    size_t thread_count() const noexcept { return num_threads_; }

    /**
     * @brief Get pool type name as string
     * @param type Pool type
     * @return String representation
     */
    static std::string pool_type_to_string(PoolType type);

    /**
     * @brief Access to individual pools (for advanced usage/benchmarking)
     */
    threadpool::ThreadPool& priority_pool() { return *priority_pool_; }
    threadpool::WorkStealingThreadPool& workstealing_pool() { return *workstealing_pool_; }
    threadpool::DependencyThreadPool& dependency_pool() { return *dependency_pool_; }

    const threadpool::ThreadPool& priority_pool() const { return *priority_pool_; }
    const threadpool::WorkStealingThreadPool& workstealing_pool() const { return *workstealing_pool_; }
    const threadpool::DependencyThreadPool& dependency_pool() const { return *dependency_pool_; }

private:
    size_t num_threads_;

    std::unique_ptr<threadpool::ThreadPool> priority_pool_;
    std::unique_ptr<threadpool::WorkStealingThreadPool> workstealing_pool_;
    std::unique_ptr<threadpool::DependencyThreadPool> dependency_pool_;
};

// ============================================================================
// Template Implementations
// ============================================================================

template<typename F, typename... Args>
auto PoolOrchestrator::submit_to_priority(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    return priority_pool_->submit(std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto PoolOrchestrator::submit_to_priority(threadpool::Priority priority, F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    return priority_pool_->submit(priority, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto PoolOrchestrator::submit_to_workstealing(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    return workstealing_pool_->submit(std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto PoolOrchestrator::submit_to_dependency(threadpool::Priority priority, F&& f, Args&&... args)
    -> std::pair<threadpool::TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
{
    return dependency_pool_->submit(priority, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto PoolOrchestrator::submit_to_dependency(threadpool::Priority priority,
                                             const std::vector<threadpool::TaskID>& dependencies,
                                             F&& f, Args&&... args)
    -> std::pair<threadpool::TaskID, std::future<typename std::invoke_result<F, Args...>::type>>
{
    return dependency_pool_->submit(priority, dependencies, std::forward<F>(f), std::forward<Args>(args)...);
}

} // namespace webservice

#endif // POOLORCHESTRATOR_HPP
