#pragma once

#include <string>

namespace Utils
{
    struct FileInfo
    {
        size_t size;
        mode_t mode;
        time_t last_modified;
        bool is_regular_file;
        bool is_directory;
    };

    class FileHandle
    {
    public:
        explicit FileHandle(const std::string &path, int flags);
        explicit FileHandle(const std::string &path, int flags, mode_t mode);
        ~FileHandle();

        ssize_t Read(void *buffer, size_t size);
        ssize_t Write(const void *buffer, size_t size);
        off_t Seek(off_t offset, int whence);
        void Sync();
        int GetFd() const;
        bool IsOpen() const;
        std::string GetPath() const;
        FileInfo GetInfo() const;

    private:
        int m_Fd{-1};
        std::string m_Path{};
    };
} // namespace Utils