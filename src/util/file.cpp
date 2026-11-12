#include "file.hpp"

#include "formattable.hpp"
#include "logging.hpp"

#include <fcntl.h>

#include <cstdlib>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef WIN32
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace srouter::util
{
    static auto logcat = log::Cat("util.file");

    std::string file_to_string(const std::filesystem::path& filename, size_t max_size)
    {
        std::ifstream in;
        std::string contents;

        in.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        in.open(filename, std::ios::binary | std::ios::in);
        in.seekg(0, std::ios::end);
        auto size = in.tellg();
        in.seekg(0, std::ios::beg);

        if (auto sz = static_cast<size_t>(size); sz > max_size)
            throw std::length_error{
                "Cannot load {}: file size {} exceeds max allowed size {}"_format(filename, sz, max_size)};

        contents.resize(size);
        in.read(contents.data(), size);
        return contents;
    }

    void buffer_to_file(const std::filesystem::path& filename, std::string_view contents)
    {
        std::ofstream out;
        out.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        out.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    void buffer_to_private_file(const std::filesystem::path& filename, std::string_view contents)
    {
#ifndef _WIN32
        // mkstemp creates a brand-new unique file with O_CREAT|O_EXCL and mode 0600, so the secret
        // is private from creation and cannot be redirected through a pre-existing symlink.
        std::string tmpl = filename.string() + ".XXXXXX";
        std::vector<char> path(tmpl.begin(), tmpl.end());
        path.push_back('\0');
        int fd = ::mkstemp(path.data());
        if (fd == -1)
            throw std::system_error{
                errno, std::generic_category(), "cannot create temp file for {}"_format(filename.string())};

        try
        {
            ::fchmod(fd, 0600);  // mkstemp is 0600 on modern libc, but be explicit
            const char* p = contents.data();
            size_t left = contents.size();
            while (left)
            {
                ssize_t n = ::write(fd, p, left);
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    throw std::system_error{
                        errno, std::generic_category(), "cannot write {}"_format(std::string{path.data()})};
                }
                p += n;
                left -= static_cast<size_t>(n);
            }
            ::close(fd);
        }
        catch (...)
        {
            ::close(fd);
            ::unlink(path.data());
            throw;
        }

        std::error_code ec;
        std::filesystem::rename(std::filesystem::path{path.data()}, filename, ec);
        if (ec)
        {
            ::unlink(path.data());
            throw std::system_error{
                ec, "cannot rename {} to {}"_format(std::string{path.data()}, filename.string())};
        }
#else
        // No POSIX mode/symlink controls here; fall back to a plain write.
        buffer_to_file(filename, contents);
#endif
    }

    static std::error_code errno_error()
    {
        int e = errno;
        errno = 0;
        return std::make_error_code(static_cast<std::errc>(e));
    }

    error_code_t EnsurePrivateFile(const std::filesystem::path& pathname)
    {
        errno = 0;
        error_code_t ec = errno_error();
        const auto str = pathname.string();
        if (exists(pathname, ec))  // file exists
        {
            permissions(pathname, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
            if (ec)
                log::error(logcat, "failed to set permissions on {}", pathname);
        }
        else  // file is not there
        {
            errno = 0;
            int fd = ::open(str.c_str(), O_RDWR | O_CREAT, 0600);
            ec = errno_error();
            if (fd != -1)
            {
                ::close(fd);
            }
        }

#ifndef WIN32
        if (ec)
            log::error(logcat, "failed to ensure {}: {}", str, ec.message());
        return ec;
#else
        return {};
#endif
    }

}  // namespace srouter::util
