#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace citysim::test {

// A fresh, empty folder under the system temp folder, removed when the object goes away. The name carries a
// counter, so several in one test run (or parallel runs) never share one.
class TempDir {
  public:
    explicit TempDir(std::string_view label) {
        static std::atomic<std::uint64_t> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("citysim_test_" + std::string(label) + "_" + std::to_string(counter.fetch_add(1)) + "_" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(&counter))); // NOLINT: differs per process
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(std::string_view name) const { return path_ / std::string(name); }

    // Writes a text file (creating folders as needed) and returns its path.
    std::filesystem::path write(std::string_view relative, std::string_view text) const {
        const std::filesystem::path file = path_ / std::string(relative);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file;
    }

  private:
    std::filesystem::path path_;
};

} // namespace citysim::test
