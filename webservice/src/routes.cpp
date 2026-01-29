#include "PoolOrchestrator.hpp"
#include "ImageProcessor.hpp"
#include "httplib.h"
#include "json.hpp"
#include <chrono>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;
using namespace webservice;

namespace {

/**
 * @brief Parse JSON request body
 */
json parse_request(const httplib::Request& req, httplib::Response& res) {
    try {
        return json::parse(req.body);
    } catch (const std::exception& e) {
        res.status = 400;
        json error = {
            {"success", false},
            {"error", "Invalid JSON: " + std::string(e.what())}
        };
        res.set_content(error.dump(2), "application/json");
        return json();
    }
}

/**
 * @brief Send JSON response
 */
void send_response(httplib::Response& res, const json& data, int status = 200) {
    res.status = status;
    res.set_content(data.dump(2), "application/json");
}

/**
 * @brief Send error response
 */
void send_error(httplib::Response& res, const std::string& message, int status = 400) {
    json error = {
        {"success", false},
        {"error", message}
    };
    send_response(res, error, status);
}

} // anonymous namespace

namespace webservice {

/**
 * @brief Register all routes with the HTTP server
 */
void register_routes(httplib::Server& server, PoolOrchestrator& orchestrator) {

    // ========================================================================
    // POST /resize - Resize image to exact dimensions
    // ========================================================================
    server.Post("/resize", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::high_resolution_clock::now();

        // Parse request
        json request = parse_request(req, res);
        if (request.is_null()) return;  // Error already sent

        try {
            // Validate required fields
            if (!request.contains("image") || !request.contains("width") || !request.contains("height")) {
                send_error(res, "Missing required fields: image, width, height");
                return;
            }

            std::string base64_image = request["image"];
            int width = request["width"];
            int height = request["height"];

            if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
                send_error(res, "Invalid dimensions: width and height must be between 1 and 10000");
                return;
            }

            // Submit resize task to priority pool
            auto future = orchestrator.submit_to_priority([base64_image, width, height]() {
                Image img = Image::from_base64(base64_image);
                Image resized = ImageProcessor::resize(img, width, height);
                return resized.to_base64(ImageFormat::PNG);
            });

            // Wait for result
            std::string result = future.get();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            // Send response
            json response = {
                {"success", true},
                {"image", result},
                {"width", width},
                {"height", height},
                {"execution_time_ms", duration.count()},
                {"pool_used", "priority"}
            };
            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Resize failed: ") + e.what(), 500);
        }
    });

    // ========================================================================
    // POST /thumbnail - Generate thumbnail with aspect ratio preservation
    // ========================================================================
    server.Post("/thumbnail", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::high_resolution_clock::now();

        json request = parse_request(req, res);
        if (request.is_null()) return;

        try {
            if (!request.contains("image")) {
                send_error(res, "Missing required field: image");
                return;
            }

            std::string base64_image = request["image"];
            int size = request.value("size", 256);  // Default 256x256

            if (size <= 0 || size > 2048) {
                send_error(res, "Invalid size: must be between 1 and 2048");
                return;
            }

            // Submit thumbnail task to priority pool
            auto future = orchestrator.submit_to_priority([base64_image, size]() {
                Image img = Image::from_base64(base64_image);
                Image thumb = ImageProcessor::thumbnail(img, size);
                return std::make_pair(thumb.to_base64(ImageFormat::PNG),
                                     std::make_pair(thumb.width, thumb.height));
            });

            auto [result, dims] = future.get();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            json response = {
                {"success", true},
                {"image", result},
                {"width", dims.first},
                {"height", dims.second},
                {"execution_time_ms", duration.count()},
                {"pool_used", "priority"}
            };
            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Thumbnail failed: ") + e.what(), 500);
        }
    });

    // ========================================================================
    // POST /filter - Apply image filters (grayscale, blur)
    // ========================================================================
    server.Post("/filter", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::high_resolution_clock::now();

        json request = parse_request(req, res);
        if (request.is_null()) return;

        try {
            if (!request.contains("image") || !request.contains("filter")) {
                send_error(res, "Missing required fields: image, filter");
                return;
            }

            std::string base64_image = request["image"];
            std::string filter = request["filter"];

            // Submit filter task to priority pool
            auto future = orchestrator.submit_to_priority([base64_image, filter, &request]() -> std::string {
                Image img = Image::from_base64(base64_image);
                Image filtered;

                if (filter == "grayscale") {
                    filtered = ImageProcessor::grayscale(img);
                } else if (filter == "blur") {
                    int radius = request.value("radius", 2);
                    if (radius < 1 || radius > 10) {
                        throw std::runtime_error("Blur radius must be between 1 and 10");
                    }
                    filtered = ImageProcessor::blur(img, radius);
                } else {
                    throw std::runtime_error("Unknown filter: " + filter + " (supported: grayscale, blur)");
                }

                return filtered.to_base64(ImageFormat::PNG);
            });

            std::string result = future.get();

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            json response = {
                {"success", true},
                {"image", result},
                {"filter", filter},
                {"execution_time_ms", duration.count()},
                {"pool_used", "priority"}
            };
            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Filter failed: ") + e.what(), 500);
        }
    });

    // ========================================================================
    // POST /pipeline - Execute a chain of operations using DependencyThreadPool
    // ========================================================================
    server.Post("/pipeline", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::high_resolution_clock::now();

        json request = parse_request(req, res);
        if (request.is_null()) return;

        try {
            if (!request.contains("image") || !request.contains("operations")) {
                send_error(res, "Missing required fields: image, operations");
                return;
            }

            std::string base64_image = request["image"];
            auto operations = request["operations"];

            if (!operations.is_array() || operations.empty()) {
                send_error(res, "operations must be a non-empty array");
                return;
            }

            // Build the pipeline using DependencyThreadPool
            // Each step depends on the previous one
            std::vector<std::string> step_names;
            std::shared_ptr<Image> current_image = std::make_shared<Image>(Image::from_base64(base64_image));

            // Submit first operation (no dependencies)
            auto process_op = [](std::shared_ptr<Image> img, const json& op) -> std::shared_ptr<Image> {
                std::string op_type = op["type"];
                auto result = std::make_shared<Image>();

                if (op_type == "resize") {
                    int w = op["width"];
                    int h = op["height"];
                    *result = ImageProcessor::resize(*img, w, h);
                } else if (op_type == "thumbnail") {
                    int size = op.value("size", 256);
                    *result = ImageProcessor::thumbnail(*img, size);
                } else if (op_type == "grayscale") {
                    *result = ImageProcessor::grayscale(*img);
                } else if (op_type == "blur") {
                    int radius = op.value("radius", 2);
                    *result = ImageProcessor::blur(*img, radius);
                } else {
                    throw std::runtime_error("Unknown operation: " + op_type);
                }
                return result;
            };

            // Execute pipeline using dependency pool
            threadpool::TaskID prev_task_id;
            std::future<std::shared_ptr<Image>> final_future;

            for (size_t i = 0; i < operations.size(); ++i) {
                const auto& op = operations[i];
                if (!op.contains("type")) {
                    send_error(res, "Each operation must have a 'type' field");
                    return;
                }
                step_names.push_back(op["type"]);

                if (i == 0) {
                    // First task - no dependencies
                    auto [task_id, future] = orchestrator.submit_to_dependency(
                        threadpool::Priority::HIGH,
                        {},  // No dependencies
                        [current_image, op, process_op]() {
                            return process_op(current_image, op);
                        }
                    );
                    prev_task_id = task_id;

                    if (operations.size() == 1) {
                        final_future = std::move(future);
                    } else {
                        // Store result for next step
                        current_image = future.get();
                    }
                } else if (i == operations.size() - 1) {
                    // Last task - depends on previous
                    auto [task_id, future] = orchestrator.submit_to_dependency(
                        threadpool::Priority::HIGH,
                        {prev_task_id},
                        [current_image, op, process_op]() {
                            return process_op(current_image, op);
                        }
                    );
                    final_future = std::move(future);
                } else {
                    // Middle task - depends on previous, creates new dependency
                    auto [task_id, future] = orchestrator.submit_to_dependency(
                        threadpool::Priority::HIGH,
                        {prev_task_id},
                        [current_image, op, process_op]() {
                            return process_op(current_image, op);
                        }
                    );
                    prev_task_id = task_id;
                    current_image = future.get();
                }
            }

            // Get final result
            auto final_image = final_future.get();
            std::string result_base64 = final_image->to_base64(ImageFormat::PNG);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            json response = {
                {"success", true},
                {"image", result_base64},
                {"width", final_image->width},
                {"height", final_image->height},
                {"operations_executed", step_names},
                {"execution_time_ms", duration.count()},
                {"pool_used", "dependency"}
            };
            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Pipeline failed: ") + e.what(), 500);
        }
    });

    // ========================================================================
    // POST /batch - Process multiple images using WorkStealingThreadPool
    // ========================================================================
    server.Post("/batch", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        auto start = std::chrono::high_resolution_clock::now();

        json request = parse_request(req, res);
        if (request.is_null()) return;

        try {
            if (!request.contains("images") || !request.contains("operation")) {
                send_error(res, "Missing required fields: images, operation");
                return;
            }

            auto images = request["images"];
            auto operation = request["operation"];

            if (!images.is_array() || images.empty()) {
                send_error(res, "images must be a non-empty array");
                return;
            }

            if (!operation.contains("type")) {
                send_error(res, "operation must have a 'type' field");
                return;
            }

            std::string op_type = operation["type"];

            // Submit all images to work-stealing pool for parallel processing
            std::vector<std::future<std::pair<std::string, std::pair<int, int>>>> futures;
            futures.reserve(images.size());

            for (const auto& img_base64 : images) {
                auto future = orchestrator.submit_to_workstealing(
                    [img_str = img_base64.get<std::string>(), op_type, operation]()
                        -> std::pair<std::string, std::pair<int, int>> {
                        Image img = Image::from_base64(img_str);
                        Image result;

                        if (op_type == "resize") {
                            int w = operation["width"];
                            int h = operation["height"];
                            result = ImageProcessor::resize(img, w, h);
                        } else if (op_type == "thumbnail") {
                            int size = operation.value("size", 256);
                            result = ImageProcessor::thumbnail(img, size);
                        } else if (op_type == "grayscale") {
                            result = ImageProcessor::grayscale(img);
                        } else if (op_type == "blur") {
                            int radius = operation.value("radius", 2);
                            result = ImageProcessor::blur(img, radius);
                        } else {
                            throw std::runtime_error("Unknown operation: " + op_type);
                        }

                        return {result.to_base64(ImageFormat::PNG),
                                {result.width, result.height}};
                    }
                );
                futures.push_back(std::move(future));
            }

            // Collect results
            json results = json::array();
            for (auto& future : futures) {
                auto [img_base64, dims] = future.get();
                results.push_back({
                    {"image", img_base64},
                    {"width", dims.first},
                    {"height", dims.second}
                });
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

            json response = {
                {"success", true},
                {"results", results},
                {"count", results.size()},
                {"operation", op_type},
                {"execution_time_ms", duration.count()},
                {"pool_used", "workstealing"}
            };
            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Batch processing failed: ") + e.what(), 500);
        }
    });

    // ========================================================================
    // GET /benchmark - Compare thread pool performance
    // ========================================================================
    server.Get("/benchmark", [&orchestrator](const httplib::Request& req, httplib::Response& res) {
        // Parameters
        int iterations = 10;
        int batch_size = 5;

        // Parse optional query params
        if (req.has_param("iterations")) {
            iterations = std::stoi(req.get_param_value("iterations"));
            iterations = std::max(1, std::min(100, iterations));
        }
        if (req.has_param("batch_size")) {
            batch_size = std::stoi(req.get_param_value("batch_size"));
            batch_size = std::max(1, std::min(20, batch_size));
        }

        try {
            // Create test image (100x100 RGB)
            Image test_image;
            test_image.width = 100;
            test_image.height = 100;
            test_image.channels = 3;
            test_image.data.resize(100 * 100 * 3);
            for (size_t i = 0; i < test_image.data.size(); i += 3) {
                test_image.data[i] = static_cast<uint8_t>(i % 256);      // R
                test_image.data[i+1] = static_cast<uint8_t>((i/3) % 256); // G
                test_image.data[i+2] = static_cast<uint8_t>((i*2) % 256); // B
            }

            auto compute_stats = [](std::vector<double>& times) {
                std::sort(times.begin(), times.end());
                double sum = std::accumulate(times.begin(), times.end(), 0.0);
                double mean = sum / times.size();
                double median = times[times.size() / 2];
                double min_val = times.front();
                double max_val = times.back();

                double sq_sum = 0;
                for (double t : times) sq_sum += (t - mean) * (t - mean);
                double stddev = std::sqrt(sq_sum / times.size());

                return json{
                    {"mean_ms", mean},
                    {"median_ms", median},
                    {"min_ms", min_val},
                    {"max_ms", max_val},
                    {"stddev_ms", stddev}
                };
            };

            // ================================================================
            // Benchmark 1: Single task submission (Priority Pool)
            // ================================================================
            std::vector<double> priority_times;
            for (int i = 0; i < iterations; ++i) {
                auto img_copy = test_image;
                auto start = std::chrono::high_resolution_clock::now();

                auto future = orchestrator.submit_to_priority([img_copy]() mutable {
                    return ImageProcessor::grayscale(img_copy);
                });
                future.get();

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                priority_times.push_back(ms);
            }

            // ================================================================
            // Benchmark 2: Batch processing (Work-Stealing Pool)
            // ================================================================
            std::vector<double> workstealing_times;
            for (int i = 0; i < iterations; ++i) {
                std::vector<Image> images(batch_size, test_image);
                auto start = std::chrono::high_resolution_clock::now();

                std::vector<std::future<Image>> futures;
                for (auto& img : images) {
                    futures.push_back(orchestrator.submit_to_workstealing(
                        [img]() mutable { return ImageProcessor::grayscale(img); }
                    ));
                }
                for (auto& f : futures) f.get();

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                workstealing_times.push_back(ms);
            }

            // ================================================================
            // Benchmark 3: Pipeline (Dependency Pool)
            // ================================================================
            std::vector<double> dependency_times;
            for (int i = 0; i < iterations; ++i) {
                auto img_copy = std::make_shared<Image>(test_image);
                auto start = std::chrono::high_resolution_clock::now();

                // Create pipeline: grayscale -> resize -> blur
                auto [id1, f1] = orchestrator.submit_to_dependency(
                    threadpool::Priority::HIGH, {},
                    [img_copy]() {
                        return std::make_shared<Image>(ImageProcessor::grayscale(*img_copy));
                    }
                );

                auto img1 = f1.get();
                auto [id2, f2] = orchestrator.submit_to_dependency(
                    threadpool::Priority::HIGH, {id1},
                    [img1]() {
                        return std::make_shared<Image>(ImageProcessor::resize(*img1, 50, 50));
                    }
                );

                auto img2 = f2.get();
                auto [id3, f3] = orchestrator.submit_to_dependency(
                    threadpool::Priority::HIGH, {id2},
                    [img2]() {
                        return std::make_shared<Image>(ImageProcessor::blur(*img2, 2));
                    }
                );
                f3.get();

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                dependency_times.push_back(ms);
            }

            // ================================================================
            // Benchmark 4: Sequential baseline (no threading)
            // ================================================================
            std::vector<double> sequential_times;
            for (int i = 0; i < iterations; ++i) {
                std::vector<Image> images(batch_size, test_image);
                auto start = std::chrono::high_resolution_clock::now();

                for (auto& img : images) {
                    img = ImageProcessor::grayscale(img);
                }

                auto end = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                sequential_times.push_back(ms);
            }

            // Compute speedups
            double seq_mean = std::accumulate(sequential_times.begin(), sequential_times.end(), 0.0) / sequential_times.size();
            double ws_mean = std::accumulate(workstealing_times.begin(), workstealing_times.end(), 0.0) / workstealing_times.size();
            double speedup = seq_mean / ws_mean;

            json response = {
                {"success", true},
                {"configuration", {
                    {"iterations", iterations},
                    {"batch_size", batch_size},
                    {"image_size", "100x100"},
                    {"thread_count", std::thread::hardware_concurrency()}
                }},
                {"benchmarks", {
                    {"priority_pool_single_task", {
                        {"description", "Single grayscale operation"},
                        {"stats", compute_stats(priority_times)}
                    }},
                    {"workstealing_pool_batch", {
                        {"description", "Batch grayscale (" + std::to_string(batch_size) + " images)"},
                        {"stats", compute_stats(workstealing_times)}
                    }},
                    {"dependency_pool_pipeline", {
                        {"description", "Pipeline: grayscale -> resize -> blur"},
                        {"stats", compute_stats(dependency_times)}
                    }},
                    {"sequential_baseline", {
                        {"description", "Sequential processing (" + std::to_string(batch_size) + " images, no threads)"},
                        {"stats", compute_stats(sequential_times)}
                    }}
                }},
                {"analysis", {
                    {"workstealing_vs_sequential_speedup", speedup},
                    {"recommendation", speedup > 1.5
                        ? "Work-stealing pool provides significant speedup for batch operations"
                        : "Consider increasing batch size for better parallelization"}
                }}
            };

            send_response(res, response);

        } catch (const std::exception& e) {
            send_error(res, std::string("Benchmark failed: ") + e.what(), 500);
        }
    });
}

} // namespace webservice
