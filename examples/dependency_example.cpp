#include "threadpool/DependencyThreadPool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <iomanip>

using namespace threadpool;

void print_header(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(70, '=') << "\n\n";
}

int main() {
    std::cout << "DependencyThreadPool - Task Dependency Management Example\n";
    std::cout << "=========================================================\n\n";

    const size_t NUM_THREADS = 4;
    DependencyThreadPool pool(NUM_THREADS);

    std::cout << "Created dependency pool with " << NUM_THREADS << " threads\n";

    // ========================================================================
    // Example 1: Simple Data Processing Pipeline (Linear Chain)
    // ========================================================================
    print_header("Example 1: Data Processing Pipeline (A -> B -> C -> D)");

    std::cout << "Demonstrating a linear dependency chain:\n";
    std::cout << "  1. Fetch data from source\n";
    std::cout << "  2. Parse and validate data\n";
    std::cout << "  3. Transform data\n";
    std::cout << "  4. Save to database\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    // Task 1: Fetch data
    auto [id1, f1] = pool.submit(Priority::HIGH, []() {
        std::cout << "[Task 1] Fetching data from source...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Task 1] Data fetched\n";
        return std::string("raw_data");
    });

    // Task 2: Parse data (depends on task 1)
    auto [id2, f2] = pool.submit(Priority::HIGH, {id1}, [](std::string data) {
        std::cout << "[Task 2] Parsing data...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Task 2] Data parsed\n";
        return data + "_parsed";
    }, f1.get());

    // Task 3: Transform data (depends on task 2)
    auto [id3, f3] = pool.submit(Priority::HIGH, {id2}, [](std::string data) {
        std::cout << "[Task 3] Transforming data...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Task 3] Data transformed\n";
        return data + "_transformed";
    }, f2.get());

    // Task 4: Save data (depends on task 3)
    auto [id4, f4] = pool.submit(Priority::HIGH, {id3}, [](std::string data) {
        std::cout << "[Task 4] Saving to database...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Task 4] Data saved\n";
        return data + "_saved";
    }, f3.get());

    std::string result1 = f4.get();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\nPipeline completed!\n";
    std::cout << "Final result: " << result1 << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";

    // ========================================================================
    // Example 2: Parallel Computation DAG (Diamond Pattern)
    // ========================================================================
    print_header("Example 2: Parallel Computation DAG (Diamond Pattern)");

    std::cout << "Demonstrating parallel DAG execution:\n";
    std::cout << "        [A] Load dataset\n";
    std::cout << "        / \\\n";
    std::cout << "     [B]   [C]\n";
    std::cout << "   Analyze  Analyze\n";
    std::cout << "   Part 1   Part 2\n";
    std::cout << "        \\ /\n";
    std::cout << "        [D] Combine results\n\n";

    start = std::chrono::high_resolution_clock::now();

    // Task A: Load dataset
    auto [idA, fA] = pool.submit([]() {
        std::cout << "[Task A] Loading dataset...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "[Task A] Dataset loaded (size: 1000000)\n";
        return 1000000;
    });

    // Get value once to share with both B and C
    int datasetSize = fA.get();

    // Task B: Analyze first half (depends on A)
    auto [idB, fB] = pool.submit({idA}, [](int size) {
        std::cout << "[Task B] Analyzing first half...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        int sum = 0;
        for (int i = 0; i < size / 2; ++i) {
            sum += i;
        }
        std::cout << "[Task B] First half analyzed: " << sum << "\n";
        return sum;
    }, datasetSize);

    // Task C: Analyze second half (depends on A, runs in parallel with B)
    auto [idC, fC] = pool.submit({idA}, [](int size) {
        std::cout << "[Task C] Analyzing second half...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        int sum = 0;
        for (int i = size / 2; i < size; ++i) {
            sum += i;
        }
        std::cout << "[Task C] Second half analyzed: " << sum << "\n";
        return sum;
    }, datasetSize);

    // Task D: Combine results (depends on both B and C)
    auto [idD, fD] = pool.submit({idB, idC}, [](int sum1, int sum2) {
        std::cout << "[Task D] Combining results...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        long long total = static_cast<long long>(sum1) + sum2;
        std::cout << "[Task D] Total sum: " << total << "\n";
        return total;
    }, fB.get(), fC.get());

    long long result2 = fD.get();

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\nDAG execution completed!\n";
    std::cout << "Final result: " << result2 << "\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "(Tasks B and C ran in parallel!)\n";

    // ========================================================================
    // Example 3: Priority Scheduling with Dependencies
    // ========================================================================
    print_header("Example 3: Priority Scheduling with Dependencies");

    std::cout << "Demonstrating priority scheduling:\n";
    std::cout << "  - Background task (LOW priority)\n";
    std::cout << "  - Normal task (MEDIUM priority)\n";
    std::cout << "  - Urgent task (HIGH priority)\n\n";

    start = std::chrono::high_resolution_clock::now();

    // Shared prerequisite task
    auto [idPrereq, fPrereq] = pool.submit(Priority::HIGH, []() {
        std::cout << "[Prerequisite] Initializing system...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "[Prerequisite] System ready\n";
        return true;
    });

    // Background task (LOW priority, depends on prerequisite)
    auto [idBg, fBg] = pool.submit(Priority::LOW, {idPrereq}, []() {
        std::cout << "[Background] Running background cleanup...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "[Background] Cleanup complete\n";
    });

    // Normal task (MEDIUM priority, depends on prerequisite)
    auto [idNormal, fNormal] = pool.submit(Priority::MEDIUM, {idPrereq}, []() {
        std::cout << "[Normal] Processing normal request...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "[Normal] Request processed\n";
    });

    // Urgent task (HIGH priority, depends on prerequisite)
    auto [idUrgent, fUrgent] = pool.submit(Priority::HIGH, {idPrereq}, []() {
        std::cout << "[Urgent] Handling urgent request...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        std::cout << "[Urgent] Request handled\n";
    });

    // Wait for all
    fPrereq.get();
    fUrgent.get();   // HIGH priority
    fNormal.get();   // MEDIUM priority
    fBg.get();       // LOW priority

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\nAll tasks completed!\n";
    std::cout << "Time: " << duration.count() << " ms\n";
    std::cout << "(Note: Urgent task executed before Normal and Background)\n";

    // ========================================================================
    // Example 4: Complex DAG - Build System Simulation
    // ========================================================================
    print_header("Example 4: Complex DAG - Build System Simulation");

    std::cout << "Simulating a build system with multiple dependencies:\n";
    std::cout << "  compile_a.cpp -> link_executable\n";
    std::cout << "  compile_b.cpp -> link_executable -> run_tests\n";
    std::cout << "  compile_c.cpp ->\n\n";

    start = std::chrono::high_resolution_clock::now();

    // Compile source files (can run in parallel)
    auto [idCompileA, fCompileA] = pool.submit(Priority::HIGH, []() {
        std::cout << "[Build] Compiling a.cpp...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        std::cout << "[Build] a.o ready\n";
        return "a.o";
    });

    auto [idCompileB, fCompileB] = pool.submit(Priority::HIGH, []() {
        std::cout << "[Build] Compiling b.cpp...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(70));
        std::cout << "[Build] b.o ready\n";
        return "b.o";
    });

    auto [idCompileC, fCompileC] = pool.submit(Priority::HIGH, []() {
        std::cout << "[Build] Compiling c.cpp...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::cout << "[Build] c.o ready\n";
        return "c.o";
    });

    // Link executable (depends on all object files)
    auto [idLink, fLink] = pool.submit(Priority::HIGH, {idCompileA, idCompileB, idCompileC},
        [](std::string a, std::string b, std::string c) {
            std::cout << "[Build] Linking executable from " << a << ", " << b << ", " << c << "...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
            std::cout << "[Build] Executable created: myapp\n";
            return "myapp";
        }, fCompileA.get(), fCompileB.get(), fCompileC.get());

    // Run tests (depends on executable)
    auto [idTest, fTest] = pool.submit(Priority::HIGH, {idLink}, [](std::string exe) {
        std::cout << "[Build] Running tests for " << exe << "...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "[Build] All tests passed!\n";
        return true;
    }, fLink.get());

    bool tests_passed = fTest.get();

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\nBuild completed!\n";
    std::cout << "Tests passed: " << (tests_passed ? "Yes" : "No") << "\n";
    std::cout << "Total build time: " << duration.count() << " ms\n";
    std::cout << "(Compilation ran in parallel, then linked, then tested)\n";

    // ========================================================================
    // Summary
    // ========================================================================
    print_header("Summary: DependencyThreadPool Benefits");

    std::cout << "Key Features Demonstrated:\n";
    std::cout << "  1. DAG Execution: Tasks automatically wait for dependencies\n";
    std::cout << "  2. Parallelism: Independent tasks run concurrently\n";
    std::cout << "  3. Priority Scheduling: Higher priority tasks execute first when ready\n";
    std::cout << "  4. Cycle Detection: Prevents circular dependencies at submission time\n";
    std::cout << "  5. Type Safety: std::future for result retrieval, TaskID for dependencies\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Data processing pipelines\n";
    std::cout << "  - Build systems\n";
    std::cout << "  - Task orchestration\n";
    std::cout << "  - Workflow automation\n";
    std::cout << "  - Parallel computation with dependencies\n\n";

    std::cout << std::string(70, '=') << "\n";

    return 0;
}
