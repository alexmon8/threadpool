#include "httplib.h"
#include "PoolOrchestrator.hpp"
#include "routes.hpp"
#include <iostream>
#include <thread>
#include <sstream>

int main() {
    const int PORT = 8080;
    const size_t NUM_THREADS = std::thread::hardware_concurrency();

    std::cout << "==========================================================\n";
    std::cout << "  Image Processing Service\n";
    std::cout << "  ThreadPool Phase 4 - Web Service Integration\n";
    std::cout << "==========================================================\n\n";

    std::cout << "Server configuration:\n";
    std::cout << "  Port: " << PORT << "\n";
    std::cout << "  Thread pool size: " << NUM_THREADS << " threads per pool\n";
    std::cout << "  Total threads: " << (NUM_THREADS * 3) << " (3 pools)\n\n";

    // Create thread pool orchestrator
    std::cout << "Initializing thread pools...\n";
    webservice::PoolOrchestrator orchestrator(NUM_THREADS);
    std::cout << "  - PriorityPool:      " << NUM_THREADS << " threads\n";
    std::cout << "  - WorkStealingPool:  " << NUM_THREADS << " threads\n";
    std::cout << "  - DependencyPool:    " << NUM_THREADS << " threads\n\n";

    // Create HTTP server
    httplib::Server server;

    // Health check endpoint
    server.Get("/health", [NUM_THREADS](const httplib::Request&, httplib::Response& res) {
        std::ostringstream json;
        json << "{\n"
             << "  \"status\": \"healthy\",\n"
             << "  \"version\": \"1.0.0\",\n"
             << "  \"server\": \"Image Processing Service\",\n"
             << "  \"threads\": " << NUM_THREADS << "\n"
             << "}";

        res.set_content(json.str(), "application/json");
    });

    // Root endpoint (welcome message)
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        const char* json = R"json({
  "message": "Image Processing Service API",
  "version": "1.0.0",
  "endpoints": {
    "GET /": "This message",
    "GET /health": "Health check",
    "GET /benchmark": "Run performance benchmarks comparing thread pools",
    "POST /resize": "Resize image to exact dimensions",
    "POST /thumbnail": "Generate thumbnail (preserves aspect ratio)",
    "POST /filter": "Apply filters (grayscale, blur)",
    "POST /pipeline": "Execute chained operations (uses DependencyThreadPool)",
    "POST /batch": "Process multiple images in parallel (uses WorkStealingThreadPool)"
  }
})json";

        res.set_content(json, "application/json");
    });

    // Register processing routes
    std::cout << "Registering routes...\n";
    webservice::register_routes(server, orchestrator);
    std::cout << "Routes registered successfully.\n\n";

    std::cout << "Starting server on http://localhost:" << PORT << "\n";
    std::cout << "Available endpoints:\n";
    std::cout << "  GET  http://localhost:" << PORT << "/\n";
    std::cout << "  GET  http://localhost:" << PORT << "/health\n";
    std::cout << "  GET  http://localhost:" << PORT << "/benchmark  (Compare pools)\n";
    std::cout << "  POST http://localhost:" << PORT << "/resize     (PriorityPool)\n";
    std::cout << "  POST http://localhost:" << PORT << "/thumbnail  (PriorityPool)\n";
    std::cout << "  POST http://localhost:" << PORT << "/filter     (PriorityPool)\n";
    std::cout << "  POST http://localhost:" << PORT << "/pipeline   (DependencyPool)\n";
    std::cout << "  POST http://localhost:" << PORT << "/batch      (WorkStealingPool)\n\n";
    std::cout << "Press Ctrl+C to stop the server...\n\n";

    // Start server (blocking call)
    bool success = server.listen("0.0.0.0", PORT);

    if (!success) {
        std::cerr << "ERROR: Failed to start server on port " << PORT << "\n";
        std::cerr << "Make sure the port is not already in use.\n";
        return 1;
    }

    return 0;
}
