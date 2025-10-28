#include "FileHandle.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <system_error>
#include <sys/stat.h>

namespace Utils
{
    FileHandle::FileHandle(const std::string &path, int flags)
        : m_Path(path), m_Fd(-1)
    {
        m_Fd = ::open(path.c_str(), flags);
        if (m_Fd == -1)
        {
            throw std::system_error(errno, std::generic_category(),
                                    "Failed to open file: " + path);
        }
    }

    FileHandle::FileHandle(const std::string &path, int flags, mode_t mode)
        : m_Path(path), m_Fd(-1)
    {
        m_Fd = ::open(path.c_str(), flags, mode);
        if (m_Fd == -1)
        {
            throw std::system_error(errno, std::generic_category(),
                                    "Failed to open file: " + path);
        }
    }

    FileHandle::~FileHandle()
    {
        if (m_Fd != -1)
        {
            ::close(m_Fd);
            m_Fd = -1;
        }
    }

    ssize_t FileHandle::Read(void *buffer, size_t size)
    {
        if (m_Fd == -1)
        {
            throw std::runtime_error("File not open for reading");
        }

        if (buffer == nullptr)
        {
            throw std::invalid_argument("Buffer cannot be null");
        }

        ssize_t bytes_read = ::read(m_Fd, buffer, size);
        if (bytes_read == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Read error");
        }
        return bytes_read;
    }

    ssize_t FileHandle::Write(const void *buffer, size_t size)
    {
        if (m_Fd == -1)
        {
            throw std::runtime_error("File not open for writing");
        }

        if (buffer == nullptr)
        {
            throw std::invalid_argument("Buffer cannot be null");
        }

        ssize_t bytes_written = ::write(m_Fd, buffer, size);

        if (bytes_written == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return -1;
            }
            throw std::system_error(errno, std::generic_category(), "Write error");
        }
        return bytes_written;
    }

    off_t FileHandle::Seek(off_t offset, int whence)
    {
        if (m_Fd == -1)
        {
            throw std::runtime_error("File not open for seeking");
        }

        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
        {
            throw std::invalid_argument("Invalid whence parameter");
        }

        off_t result = ::lseek(m_Fd, offset, whence);
        if (result == (off_t)-1)
        {
            throw std::system_error(errno, std::generic_category(), "Seek error");
        }
        return result;
    }

    void FileHandle::Sync()
    {
        if (m_Fd == -1)
        {
            throw std::runtime_error("File not open for syncing");
        }

        if (::fsync(m_Fd) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Sync error");
        }
    }

    int FileHandle::GetFd() const
    {
        return m_Fd;
    }

    bool FileHandle::IsOpen() const
    {
        return m_Fd != -1;
    }

    std::string FileHandle::GetPath() const
    {
        return m_Path;
    }

    FileInfo FileHandle::GetInfo() const
    {
        if (m_Fd == -1)
        {
            throw std::runtime_error("File not open for getting info");
        }

        struct stat stat_buf;
        if (::fstat(m_Fd, &stat_buf) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "fstat error");
        }

        FileInfo info;
        info.size = static_cast<size_t>(stat_buf.st_size);
        info.mode = stat_buf.st_mode;
        info.last_modified = stat_buf.st_mtime;
        info.is_regular_file = S_ISREG(stat_buf.st_mode);
        info.is_directory = S_ISDIR(stat_buf.st_mode);

        return info;
    }
}