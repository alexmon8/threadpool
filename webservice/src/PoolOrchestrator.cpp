#include "PoolOrchestrator.hpp"
#include "threadpool/ThreadPool.hpp"
#include "threadpool/WorkStealingThreadPool.hpp"
#include "threadpool/DependencyThreadPool.hpp"
#include <stdexcept>

namespace webservice {

PoolOrchestrator::PoolOrchestrator(size_t num_threads)
    : num_threads_(num_threads)
{
    if (num_threads == 0) {
        throw std::invalid_argument("PoolOrchestrator requires at least 1 thread");
    }

    // Initialize all three thread pools with the same thread count
    priority_pool_ = std::make_unique<threadpool::ThreadPool>(num_threads);
    workstealing_pool_ = std::make_unique<threadpool::WorkStealingThreadPool>(num_threads);
    dependency_pool_ = std::make_unique<threadpool::DependencyThreadPool>(num_threads);
}

PoolOrchestrator::~PoolOrchestrator() {
    // Pools will be destroyed in reverse order of construction
    // Their destructors handle graceful shutdown
}

PoolType PoolOrchestrator::select_pool_for_operation(const std::string& operation,
                                                       size_t batch_size) const {
    // Pool selection strategy based on operation characteristics

    // Pipeline operations → DependencyThreadPool (DAG execution)
    if (operation == "pipeline") {
        return PoolType::DEPENDENCY;
    }

    // Batch operations → WorkStealingThreadPool (automatic load balancing)
    if (operation == "batch" || batch_size > 1) {
        return PoolType::WORKSTEALING;
    }

    // Single operations → Priority pool (simple, priority-based)
    // Operations: resize, thumbnail, filter, convert, grayscale, blur
    return PoolType::PRIORITY;
}

std::string PoolOrchestrator::pool_type_to_string(PoolType type) {
    switch (type) {
        case PoolType::PRIORITY:
            return "priority";
        case PoolType::WORKSTEALING:
            return "workstealing";
        case PoolType::DEPENDENCY:
            return "dependency";
        default:
            return "unknown";
    }
}

} // namespace webservice
