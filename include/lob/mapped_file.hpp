#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace lob {

// Read-only memory-mapped view of a file. Supports POSIX mmap and the
// Windows file-mapping API. Throws on any I/O error -- this is a one-shot
// helper used by the replay harness, not a hot-path object.
class MappedFile {
public:
    explicit MappedFile(const std::string& path) {
#if defined(_WIN32)
        file_ = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("MappedFile: cannot open " + path);
        }
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(file_, &sz)) {
            CloseHandle(file_);
            throw std::runtime_error("MappedFile: cannot stat " + path);
        }
        size_ = static_cast<std::size_t>(sz.QuadPart);
        mapping_ = CreateFileMappingA(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_ == nullptr) {
            CloseHandle(file_);
            throw std::runtime_error("MappedFile: cannot map " + path);
        }
        data_ = static_cast<const std::uint8_t*>(
            MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (data_ == nullptr) {
            CloseHandle(mapping_);
            CloseHandle(file_);
            throw std::runtime_error("MappedFile: cannot view " + path);
        }
#else
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) throw std::runtime_error("MappedFile: cannot open " + path);
        struct stat st {};
        if (::fstat(fd_, &st) < 0) {
            ::close(fd_);
            throw std::runtime_error("MappedFile: cannot stat " + path);
        }
        size_ = static_cast<std::size_t>(st.st_size);
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) {
            ::close(fd_);
            throw std::runtime_error("MappedFile: cannot mmap " + path);
        }
        data_ = static_cast<const std::uint8_t*>(p);
        // Hint the kernel to prefetch sequentially; ITCH replay is purely streaming.
        ::madvise(p, size_, MADV_SEQUENTIAL);
#endif
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    ~MappedFile() {
#if defined(_WIN32)
        if (data_)    UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
        if (data_) ::munmap(const_cast<std::uint8_t*>(data_), size_);
        if (fd_ >= 0) ::close(fd_);
#endif
    }

    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t         size() const noexcept { return size_; }

private:
    const std::uint8_t* data_{nullptr};
    std::size_t         size_{0};
#if defined(_WIN32)
    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{nullptr};
#else
    int    fd_{-1};
#endif
};

}  // namespace lob
