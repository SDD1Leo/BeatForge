// The Linux transport of IpcServer: a Unix domain socket (see IpcServer.h).
#include "beatforge/ipc/IpcServer.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace beatforge::ipc
{
    using Json = nlohmann::json;

    struct IpcServer::Platform
    {
        int listenFd = -1, wakeRead = -1, wakeWrite = -1;
    };

    //==============================================================================
    std::filesystem::path IpcServer::defaultPath()
    {
        if (const char* explicitPath = std::getenv ("BEATFORGE_SOCKET"); explicitPath != nullptr && *explicitPath != 0)
            return explicitPath;
        if (const char* runtime = std::getenv ("XDG_RUNTIME_DIR"); runtime != nullptr && *runtime != 0)
            return std::filesystem::path (runtime) / "beatforge" / "daw.sock";
        return std::filesystem::path ("/tmp") / ("beatforge-" + std::to_string (getuid())) / "daw.sock";
    }

    IpcServer::IpcServer (Handler h) : os (std::make_unique<Platform>()), handler (std::move (h)) {}
    IpcServer::~IpcServer() { stop(); }

    namespace
    {
        bool fillAddress (const std::filesystem::path& p, sockaddr_un& addr)
        {
            std::memset (&addr, 0, sizeof (addr));
            addr.sun_family = AF_UNIX;
            const auto s = p.string();
            if (s.size() >= sizeof (addr.sun_path))
                return false;
            std::memcpy (addr.sun_path, s.c_str(), s.size());
            return true;
        }

        std::string errorLine (const char* code, const char* message)
        {
            return Json { { "id", nullptr }, { "ok", false }, { "error", { { "code", code }, { "message", message } } } }.dump() + "\n";
        }

        bool sendAll (int fd, const std::string& data)
        {
            size_t sent = 0;
            while (sent < data.size())
            {
                const auto n = ::send (fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return false;
                sent += (size_t) n;
            }
            return true;
        }
    }

    bool IpcServer::start (const std::filesystem::path& socketPath, std::string& error)
    {
        stop();
        path = socketPath;
        sockaddr_un addr;
        if (! fillAddress (path, addr))
            return error = "socket path too long: " + path.string(), false;

        // Private folder: create it 0700, or accept an existing one only if it is ours and private.
        const auto dir = path.parent_path();
        if (::mkdir (dir.c_str(), 0700) != 0 && errno != EEXIST)
            return error = "cannot create " + dir.string() + ": " + std::strerror (errno), false;
        struct stat st {};
        if (::stat (dir.c_str(), &st) != 0 || st.st_uid != getuid() || (st.st_mode & 077) != 0)
            return error = dir.string() + " must be a folder owned by you and private (mode 0700)", false;

        // A live server answers on the path: never take it over. A dead one left a stale file.
        if (std::filesystem::exists (path))
        {
            const int probe = ::socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            const bool alive = probe >= 0 && ::connect (probe, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) == 0;
            if (probe >= 0) ::close (probe);
            if (alive)
                return error = "another BeatForge is already connected to the AI bridge (" + path.string() + ")", false;
            ::unlink (path.c_str());
        }

        os->listenFd = ::socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (os->listenFd < 0)
            return error = std::string ("socket: ") + std::strerror (errno), false;
        const auto oldMask = ::umask (077);   // the socket file is created 0600
        const bool bound = ::bind (os->listenFd, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) == 0;
        ::umask (oldMask);
        if (! bound || ::listen (os->listenFd, 4) != 0)
        {
            error = std::string ("cannot listen on ") + path.string() + ": " + std::strerror (errno);
            ::close (os->listenFd);
            os->listenFd = -1;
            return false;
        }
        ::chmod (path.c_str(), 0600);

        int pipeFds[2];
        if (::pipe2 (pipeFds, O_CLOEXEC | O_NONBLOCK) != 0)
        {
            error = std::string ("pipe: ") + std::strerror (errno);
            ::close (os->listenFd);
            os->listenFd = -1;
            ::unlink (path.c_str());
            return false;
        }
        os->wakeRead = pipeFds[0];
        os->wakeWrite = pipeFds[1];

        running = true;
        thread = std::thread ([this] { loop(); });
        return true;
    }

    void IpcServer::stop()
    {
        if (! thread.joinable())
            return;
        const char byte = 1;
        [[maybe_unused]] const auto ignored = ::write (os->wakeWrite, &byte, 1);
        thread.join();
        ::close (os->wakeRead);
        ::close (os->wakeWrite);
        ::close (os->listenFd);
        os->wakeRead = os->wakeWrite = os->listenFd = -1;
        ::unlink (path.c_str());
        running = false;
        clients = 0;
    }

    void IpcServer::loop()
    {
        struct Client { int fd; std::string buffer; };
        std::vector<Client> open;

        auto closeClient = [&] (size_t i)
        {
            ::close (open[i].fd);
            open.erase (open.begin() + (std::ptrdiff_t) i);
            clients = (int) open.size();
        };

        for (;;)
        {
            std::vector<pollfd> fds { { os->wakeRead, POLLIN, 0 }, { os->listenFd, POLLIN, 0 } };
            for (auto& c : open)
                fds.push_back ({ c.fd, POLLIN, 0 });

            if (::poll (fds.data(), fds.size(), -1) < 0)
            {
                if (errno == EINTR) continue;
                break;
            }
            if ((fds[0].revents & POLLIN) != 0)
                break;   // stop()

            if ((fds[1].revents & POLLIN) != 0)
            {
                const int fd = ::accept4 (os->listenFd, nullptr, nullptr, SOCK_CLOEXEC);
                if (fd >= 0)
                {
                    ucred cred {};
                    socklen_t len = sizeof (cred);
                    const bool sameUser = ::getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.uid == getuid();
                    if (! sameUser || (int) open.size() >= maxClients)
                    {
                        sendAll (fd, errorLine ("refused", sameUser ? "too many connections" : "not allowed"));
                        ::close (fd);
                    }
                    else
                    {
                        open.push_back ({ fd, {} });
                        clients = (int) open.size();
                    }
                }
            }

            // Clients, newest first so closing keeps indices valid.
            for (size_t k = fds.size(); k-- > 2;)
            {
                const auto i = k - 2;
                if ((fds[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
                    continue;

                char data[8192];
                const auto n = ::recv (open[i].fd, data, sizeof (data), 0);
                if (n <= 0)
                {
                    closeClient (i);
                    continue;
                }
                open[i].buffer.append (data, (size_t) n);

                bool keep = true;
                for (size_t nl; keep && (nl = open[i].buffer.find ('\n')) != std::string::npos;)
                {
                    const auto line = open[i].buffer.substr (0, nl);
                    open[i].buffer.erase (0, nl + 1);
                    if (line.find_first_not_of (" \t\r") == std::string::npos)
                        continue;
                    ++requests;
                    keep = sendAll (open[i].fd, handler (line) + "\n");
                }
                if (keep && open[i].buffer.size() > maxLineBytes)
                {
                    sendAll (open[i].fd, errorLine ("bad_request", "request line too long (max 1 MiB)"));
                    keep = false;
                }
                if (! keep)
                    closeClient (i);
            }
        }

        for (auto& c : open)
            ::close (c.fd);
        clients = 0;
    }
}
