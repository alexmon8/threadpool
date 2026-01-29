#ifndef IMAGEPROCESSOR_HPP
#define IMAGEPROCESSOR_HPP

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>

namespace webservice {

/**
 * @brief Supported image formats
 */
enum class ImageFormat {
    PNG,
    JPEG,
    UNKNOWN
};

/**
 * @brief Image data container
 *
 * Holds raw image data with metadata. Supports conversion to/from base64
 * for HTTP transport.
 */
struct Image {
    int width;
    int height;
    int channels;  // 1=grayscale, 3=RGB, 4=RGBA
    std::vector<uint8_t> data;

    Image() : width(0), height(0), channels(0) {}

    Image(int w, int h, int c)
        : width(w), height(h), channels(c), data(w * h * c) {}

    Image(int w, int h, int c, const std::vector<uint8_t>& d)
        : width(w), height(h), channels(c), data(d) {}

    /**
     * @brief Check if image is valid
     */
    bool is_valid() const {
        return width > 0 && height > 0 && channels > 0 &&
               data.size() == static_cast<size_t>(width * height * channels);
    }

    /**
     * @brief Get size in bytes
     */
    size_t size_bytes() const {
        return data.size();
    }

    /**
     * @brief Create image from base64-encoded data
     * @param base64 Base64-encoded image data
     * @return Decoded image
     * @throws std::runtime_error if decoding or loading fails
     */
    static Image from_base64(const std::string& base64);

    /**
     * @brief Convert image to base64-encoded string
     * @param format Output format (PNG or JPEG)
     * @param quality JPEG quality (0-100, ignored for PNG)
     * @return Base64-encoded image data
     * @throws std::runtime_error if encoding fails
     */
    std::string to_base64(ImageFormat format = ImageFormat::PNG, int quality = 90) const;
};

/**
 * @brief Image processing operations using stb_image
 *
 * Provides basic image manipulation: loading, saving, resizing, and filtering.
 * All operations are thread-safe (stateless functions).
 */
class ImageProcessor {
public:
    /**
     * @brief Load image from file path
     * @param path File path to image
     * @return Loaded image
     * @throws std::runtime_error if loading fails
     */
    static Image load(const std::string& path);

    /**
     * @brief Load image from memory buffer
     * @param data Pointer to image data in memory
     * @param size Size of data in bytes
     * @return Loaded image
     * @throws std::runtime_error if loading fails
     */
    static Image load_from_memory(const uint8_t* data, size_t size);

    /**
     * @brief Save image to file
     * @param img Image to save
     * @param path Output file path
     * @param format Output format
     * @param quality JPEG quality (0-100, ignored for PNG)
     * @throws std::runtime_error if saving fails
     */
    static void save(const Image& img, const std::string& path,
                     ImageFormat format = ImageFormat::PNG, int quality = 90);

    /**
     * @brief Encode image to memory buffer
     * @param img Image to encode
     * @param format Output format
     * @param quality JPEG quality (0-100, ignored for PNG)
     * @return Encoded image data
     * @throws std::runtime_error if encoding fails
     */
    static std::vector<uint8_t> encode_to_memory(const Image& img,
                                                   ImageFormat format = ImageFormat::PNG,
                                                   int quality = 90);

    /**
     * @brief Resize image to new dimensions
     * @param src Source image
     * @param new_width Target width
     * @param new_height Target height
     * @return Resized image
     * @throws std::runtime_error if resize fails
     */
    static Image resize(const Image& src, int new_width, int new_height);

    /**
     * @brief Create thumbnail with fixed size (maintains aspect ratio)
     * @param src Source image
     * @param size Thumbnail size (width and height)
     * @return Thumbnail image
     * @throws std::runtime_error if resize fails
     */
    static Image thumbnail(const Image& src, int size = 256);

    /**
     * @brief Convert image to grayscale
     * @param src Source image
     * @return Grayscale image
     */
    static Image grayscale(const Image& src);

    /**
     * @brief Apply simple box blur filter
     * @param src Source image
     * @param radius Blur radius (1-10)
     * @return Blurred image
     */
    static Image blur(const Image& src, int radius = 2);

    /**
     * @brief Detect image format from file extension
     * @param path File path
     * @return Detected format
     */
    static ImageFormat detect_format(const std::string& path);
};

/**
 * @brief Base64 encoding/decoding utilities
 */
class Base64 {
public:
    /**
     * @brief Encode binary data to base64 string
     * @param data Pointer to binary data
     * @param size Size of data in bytes
     * @return Base64-encoded string
     */
    static std::string encode(const uint8_t* data, size_t size);

    /**
     * @brief Decode base64 string to binary data
     * @param base64 Base64-encoded string
     * @return Decoded binary data
     * @throws std::runtime_error if decoding fails
     */
    static std::vector<uint8_t> decode(const std::string& base64);
};

} // namespace webservice

#endif // IMAGEPROCESSOR_HPP
