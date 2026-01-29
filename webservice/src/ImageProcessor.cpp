#include "ImageProcessor.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>

// Include stb headers (implementations are in stb_impl.cpp)
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

namespace webservice {

// ============================================================================
// Base64 Implementation
// ============================================================================

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string Base64::encode(const uint8_t* data, size_t size) {
    std::string result;
    result.reserve(((size + 2) / 3) * 4);

    for (size_t i = 0; i < size; i += 3) {
        uint32_t triple = (data[i] << 16);
        if (i + 1 < size) triple |= (data[i + 1] << 8);
        if (i + 2 < size) triple |= data[i + 2];

        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back((i + 1 < size) ? base64_chars[(triple >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < size) ? base64_chars[triple & 0x3F] : '=');
    }

    return result;
}

std::vector<uint8_t> Base64::decode(const std::string& base64) {
    // Create decode table
    static int decode_table[256];
    static bool table_initialized = false;
    if (!table_initialized) {
        std::fill(std::begin(decode_table), std::end(decode_table), -1);
        for (int i = 0; i < 64; ++i) {
            decode_table[static_cast<uint8_t>(base64_chars[i])] = i;
        }
        table_initialized = true;
    }

    std::vector<uint8_t> result;
    result.reserve((base64.size() / 4) * 3);

    for (size_t i = 0; i < base64.size(); i += 4) {
        uint32_t triple = 0;
        int padding = 0;

        for (int j = 0; j < 4; ++j) {
            if (i + j >= base64.size() || base64[i + j] == '=') {
                padding++;
            } else {
                int value = decode_table[static_cast<uint8_t>(base64[i + j])];
                if (value == -1) {
                    throw std::runtime_error("Invalid base64 character");
                }
                triple |= (value << (18 - j * 6));
            }
        }

        result.push_back((triple >> 16) & 0xFF);
        if (padding < 2) result.push_back((triple >> 8) & 0xFF);
        if (padding < 1) result.push_back(triple & 0xFF);
    }

    return result;
}

// ============================================================================
// Image Implementation
// ============================================================================

Image Image::from_base64(const std::string& base64) {
    // Decode base64 to binary
    std::vector<uint8_t> binary = Base64::decode(base64);

    // Load image from binary data
    return ImageProcessor::load_from_memory(binary.data(), binary.size());
}

std::string Image::to_base64(ImageFormat format, int quality) const {
    if (!is_valid()) {
        throw std::runtime_error("Cannot encode invalid image");
    }

    // Encode image to memory
    std::vector<uint8_t> encoded = ImageProcessor::encode_to_memory(*this, format, quality);

    // Convert to base64
    return Base64::encode(encoded.data(), encoded.size());
}

// ============================================================================
// ImageProcessor Implementation
// ============================================================================

Image ImageProcessor::load(const std::string& path) {
    int width, height, channels;
    uint8_t* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        throw std::runtime_error(std::string("Failed to load image: ") + stbi_failure_reason());
    }

    // Copy data to vector and free stbi memory
    size_t data_size = width * height * channels;
    std::vector<uint8_t> image_data(data, data + data_size);
    stbi_image_free(data);

    return Image(width, height, channels, image_data);
}

Image ImageProcessor::load_from_memory(const uint8_t* data, size_t size) {
    int width, height, channels;
    uint8_t* img_data = stbi_load_from_memory(data, static_cast<int>(size),
                                               &width, &height, &channels, 0);

    if (!img_data) {
        throw std::runtime_error(std::string("Failed to load image from memory: ") +
                                 stbi_failure_reason());
    }

    // Copy data to vector and free stbi memory
    size_t data_size = width * height * channels;
    std::vector<uint8_t> image_data(img_data, img_data + data_size);
    stbi_image_free(img_data);

    return Image(width, height, channels, image_data);
}

void ImageProcessor::save(const Image& img, const std::string& path,
                           ImageFormat format, int quality) {
    if (!img.is_valid()) {
        throw std::runtime_error("Cannot save invalid image");
    }

    int success = 0;
    if (format == ImageFormat::PNG) {
        success = stbi_write_png(path.c_str(), img.width, img.height, img.channels,
                                  img.data.data(), img.width * img.channels);
    } else if (format == ImageFormat::JPEG) {
        success = stbi_write_jpg(path.c_str(), img.width, img.height, img.channels,
                                  img.data.data(), quality);
    } else {
        throw std::runtime_error("Unsupported image format");
    }

    if (!success) {
        throw std::runtime_error("Failed to save image to " + path);
    }
}

// Write callback for stbi_write_png_to_func
static void write_callback(void* context, void* data, int size) {
    auto* buffer = static_cast<std::vector<uint8_t>*>(context);
    uint8_t* bytes = static_cast<uint8_t*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

std::vector<uint8_t> ImageProcessor::encode_to_memory(const Image& img,
                                                        ImageFormat format,
                                                        int quality) {
    if (!img.is_valid()) {
        throw std::runtime_error("Cannot encode invalid image");
    }

    std::vector<uint8_t> buffer;

    if (format == ImageFormat::PNG) {
        stbi_write_png_to_func(write_callback, &buffer, img.width, img.height,
                                img.channels, img.data.data(), img.width * img.channels);
    } else if (format == ImageFormat::JPEG) {
        stbi_write_jpg_to_func(write_callback, &buffer, img.width, img.height,
                                img.channels, img.data.data(), quality);
    } else {
        throw std::runtime_error("Unsupported image format for encoding");
    }

    if (buffer.empty()) {
        throw std::runtime_error("Failed to encode image to memory");
    }

    return buffer;
}

Image ImageProcessor::resize(const Image& src, int new_width, int new_height) {
    if (!src.is_valid()) {
        throw std::runtime_error("Cannot resize invalid image");
    }

    if (new_width <= 0 || new_height <= 0) {
        throw std::runtime_error("Invalid resize dimensions");
    }

    Image result(new_width, new_height, src.channels);

    // Use stb_image_resize2 for high-quality resizing
    void* resize_result = stbir_resize_uint8_linear(
        src.data.data(), src.width, src.height, 0,
        result.data.data(), new_width, new_height, 0,
        static_cast<stbir_pixel_layout>(src.channels)
    );

    if (!resize_result) {
        throw std::runtime_error("Failed to resize image");
    }

    return result;
}

Image ImageProcessor::thumbnail(const Image& src, int size) {
    if (!src.is_valid()) {
        throw std::runtime_error("Cannot create thumbnail of invalid image");
    }

    // Calculate dimensions maintaining aspect ratio
    int new_width, new_height;
    if (src.width > src.height) {
        new_width = size;
        new_height = static_cast<int>((static_cast<float>(src.height) / src.width) * size);
    } else {
        new_height = size;
        new_width = static_cast<int>((static_cast<float>(src.width) / src.height) * size);
    }

    return resize(src, new_width, new_height);
}

Image ImageProcessor::grayscale(const Image& src) {
    if (!src.is_valid()) {
        throw std::runtime_error("Cannot convert invalid image to grayscale");
    }

    // If already grayscale, return copy
    if (src.channels == 1) {
        return src;
    }

    Image result(src.width, src.height, 1);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            int src_idx = (y * src.width + x) * src.channels;
            int dst_idx = y * src.width + x;

            // Use standard luminance weights (BT.709)
            float r = src.data[src_idx];
            float g = src.data[src_idx + 1];
            float b = src.data[src_idx + 2];

            uint8_t gray = static_cast<uint8_t>(0.2126f * r + 0.7152f * g + 0.0722f * b);
            result.data[dst_idx] = gray;
        }
    }

    return result;
}

Image ImageProcessor::blur(const Image& src, int radius) {
    if (!src.is_valid()) {
        throw std::runtime_error("Cannot blur invalid image");
    }

    if (radius < 1 || radius > 10) {
        throw std::runtime_error("Blur radius must be between 1 and 10");
    }

    Image result(src.width, src.height, src.channels);

    // Simple box blur (separable: horizontal then vertical)
    int kernel_size = radius * 2 + 1;
    float weight = 1.0f / kernel_size;

    // Temporary buffer for horizontal pass
    std::vector<float> temp(src.data.size());

    // Horizontal pass
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            for (int c = 0; c < src.channels; ++c) {
                float sum = 0.0f;
                for (int dx = -radius; dx <= radius; ++dx) {
                    int sx = std::clamp(x + dx, 0, src.width - 1);
                    sum += src.data[(y * src.width + sx) * src.channels + c];
                }
                temp[(y * src.width + x) * src.channels + c] = sum * weight;
            }
        }
    }

    // Vertical pass
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            for (int c = 0; c < src.channels; ++c) {
                float sum = 0.0f;
                for (int dy = -radius; dy <= radius; ++dy) {
                    int sy = std::clamp(y + dy, 0, src.height - 1);
                    sum += temp[(sy * src.width + x) * src.channels + c];
                }
                result.data[(y * src.width + x) * src.channels + c] =
                    static_cast<uint8_t>(std::clamp(sum * weight, 0.0f, 255.0f));
            }
        }
    }

    return result;
}

ImageFormat ImageProcessor::detect_format(const std::string& path) {
    std::string lower_path = path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

    // C++17 compatible suffix check
    auto has_suffix = [](const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (has_suffix(lower_path, ".png")) return ImageFormat::PNG;
    if (has_suffix(lower_path, ".jpg") || has_suffix(lower_path, ".jpeg")) return ImageFormat::JPEG;

    return ImageFormat::UNKNOWN;
}

} // namespace webservice
