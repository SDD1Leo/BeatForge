// The Windows transport of IpcServer: a named pipe (see IpcServer.h).
#include "beatforge/ipc/IpcServer.h"
#include "beatforge/util/Paths.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace beatforge::ipc
{
    using Json = nlohmann::json;

    struct IpcServer::Platform
    {
        struct ClientThread
        {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> done;
        };

        HANDLE stopEvent = nullptr;
        HANDLE firstInstance = INVALID_HANDLE_VALUE;
        PSECURITY_DESCRIPTOR security = nullptr;   // only the current user may open the pipe
        std::wstring name, userSid;
        std::mutex handlerLock;                    // requests run one at a time, as on Linux
        std::vector<ClientThread> clientThreads;   // accept thread only
    };

    namespace
    {
        constexpr DWORD bufferBytes = 64 * 1024;

        std::wstring sidOfToken (HANDLE token)
        {
            DWORD size = 0;
            GetTokenInformation (token, TokenUser, nullptr, 0, &size);
            if (size == 0)
                return {};
            std::vector<BYTE> buffer (size);
            std::wstring result;
            if (GetTokenInformation (token, TokenUser, buffer.data(), size, &size))
            {
                LPWSTR text = nullptr;
                if (ConvertSidToStringSidW (reinterpret_cast<TOKEN_USER*> (buffer.data())->User.Sid, &text))
                {
                    result = text;
                    LocalFree (text);
                }
            }
            return result;
        }

        std::wstring sidOfProcess (HANDLE process)
        {
            HANDLE token = nullptr;
            if (! OpenProcessToken (process, TOKEN_QUERY, &token))
                return {};
            auto sid = sidOfToken (token);
            CloseHandle (token);
            return sid;
        }

        // The client's user, or empty if it cannot be read (the pipe's security
        // descriptor already keeps other users out).
        std::wstring clientSid (HANDLE pipe)
        {
            ULONG pid = 0;
            if (! GetNamedPipeClientProcessId (pipe, &pid))
                return {};
            HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process == nullptr)
                return {};
            auto sid = sidOfProcess (process);
            CloseHandle (process);
            return sid;
        }

        std::string utf8 (const std::wstring& w) { return pathToUtf8 (std::filesystem::path (w)); }

        std::string lastError()
        {
            const auto code = GetLastError();
            LPWSTR text = nullptr;
            FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, code, 0, reinterpret_cast<LPWSTR> (&text), 0, nullptr);
            std::wstring message = text != nullptr ? text : L"";
            if (text != nullptr)
                LocalFree (text);
            while (! message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.'))
                message.pop_back();
            return utf8 (message) + " (error " + std::to_string (code) + ")";
        }

        std::string errorLine (const char* code, const char* message)
        {
            return Json { { "id", nullptr }, { "ok", false }, { "error", { { "code", code }, { "message", message } } } }.dump() + "\n";
        }

        bool writeAll (HANDLE pipe, const std::string& data)
        {
            OVERLAPPED ov {};
            ov.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
            bool ok = ov.hEvent != nullptr;
            for (size_t sent = 0; ok && sent < data.size();)
            {
                ResetEvent (ov.hEvent);
                DWORD n = 0;
                const auto chunk = (DWORD) std::min<size_t> (data.size() - sent, bufferBytes);
                if (! WriteFile (pipe, data.data() + sent, chunk, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
                    ok = false;
                else if (! GetOverlappedResult (pipe, &ov, &n, TRUE) || n == 0)
                    ok = false;
                else
                    sent += n;
            }
            if (ov.hEvent != nullptr)
                CloseHandle (ov.hEvent);
            return ok;
        }

        void closePipe (HANDLE pipe)
        {
            FlushFileBuffers (pipe);
            DisconnectNamedPipe (pipe);
            CloseHandle (pipe);
        }
    }

    //==============================================================================
    std::filesystem::path IpcServer::defaultPath()
    {
        if (const wchar_t* explicitPath = _wgetenv (L"BEATFORGE_SOCKET"); explicitPath != nullptr && *explicitPath != 0)
            return std::filesystem::path (explicitPath);

        wchar_t user[256] {};
        DWORD size = (DWORD) std::size (user);
        std::wstring name = GetUserNameW (user, &size) ? user : L"user";
        std::replace (name.begin(), name.end(), L'\\', L'_');   // a backslash would end the pipe name
        return std::filesystem::path (L"\\\\.\\pipe\\beatforge-" + name);
    }

    IpcServer::IpcServer (Handler h) : os (std::make_unique<Platform>()), handler (std::move (h)) {}
    IpcServer::~IpcServer() { stop(); }

    namespace
    {
        HANDLE createInstance (const std::wstring& name, PSECURITY_DESCRIPTOR security, bool first)
        {
            SECURITY_ATTRIBUTES attributes { sizeof (SECURITY_ATTRIBUTES), security, FALSE };
            return CreateNamedPipeW (name.c_str(),
                                     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     PIPE_UNLIMITED_INSTANCES, bufferBytes, bufferBytes, 0, &attributes);
        }
    }

    bool IpcServer::start (const std::filesystem::path& pipePath, std::string& error)
    {
        stop();
        path = pipePath;
        os->name = pipePath.wstring();
        if (os->name.rfind (L"\\\\.\\pipe\\", 0) != 0)
            return error = "not a pipe name: " + pathToUtf8 (path) + " (expected \\\\.\\pipe\\...)", false;

        // Only this user (by SID) may open the pipe.
        HANDLE token = nullptr;
        if (OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            os->userSid = sidOfToken (token);
            CloseHandle (token);
        }
        if (os->userSid.empty())
            return error = "cannot read the current user: " + lastError(), false;
        const auto sddl = L"D:P(A;;GA;;;" + os->userSid + L")";
        if (! ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl.c_str(), SDDL_REVISION_1, &os->security, nullptr))
            return error = "cannot build the pipe's security: " + lastError(), false;

        // The first instance fails if any BeatForge already serves this name.
        os->firstInstance = createInstance (os->name, os->security, true);
        if (os->firstInstance == INVALID_HANDLE_VALUE)
        {
            const bool taken = GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_PIPE_BUSY;
            error = taken ? "another BeatForge is already connected to the AI bridge (" + pathToUtf8 (path) + ")"
                          : "cannot create " + pathToUtf8 (path) + ": " + lastError();
            LocalFree (os->security);
            os->security = nullptr;
            return false;
        }

        os->stopEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        if (os->stopEvent == nullptr)
        {
            error = "cannot create an event: " + lastError();
            CloseHandle (os->firstInstance);
            os->firstInstance = INVALID_HANDLE_VALUE;
            LocalFree (os->security);
            os->security = nullptr;
            return false;
        }

        running = true;
        thread = std::thread ([this] { loop(); });
        return true;
    }

    void IpcServer::stop()
    {
        if (! thread.joinable())
            return;
        SetEvent (os->stopEvent);
        thread.join();   // also joins the client threads
        CloseHandle (os->stopEvent);
        os->stopEvent = nullptr;
        LocalFree (os->security);
        os->security = nullptr;
        running = false;
        clients = 0;
    }

    void IpcServer::loop()
    {
        // One request line at a time per client, answered in order; the handler
        // is serialised across clients.
        auto serve = [this] (HANDLE pipe, std::shared_ptr<std::atomic<bool>> done)
        {
            OVERLAPPED ov {};
            ov.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
            std::string buffer;
            char data[8192];

            for (bool keep = ov.hEvent != nullptr; keep;)
            {
                ResetEvent (ov.hEvent);
                DWORD n = 0;
                if (! ReadFile (pipe, data, sizeof (data), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
                    break;   // the client went away
                const HANDLE waits[] { os->stopEvent, ov.hEvent };
                if (WaitForMultipleObjects (2, waits, FALSE, INFINITE) == WAIT_OBJECT_0)
                {
                    CancelIoEx (pipe, &ov);
                    GetOverlappedResult (pipe, &ov, &n, TRUE);
                    break;
                }
                if (! GetOverlappedResult (pipe, &ov, &n, FALSE) || n == 0)
                    break;
                buffer.append (data, n);

                for (size_t nl; keep && (nl = buffer.find ('\n')) != std::string::npos;)
                {
                    const auto line = buffer.substr (0, nl);
                    buffer.erase (0, nl + 1);
                    if (line.find_first_not_of (" \t\r") == std::string::npos)
                        continue;
                    ++requests;
                    std::string response;
                    {
                        const std::scoped_lock lock (os->handlerLock);
                        response = handler (line);
                    }
                    keep = writeAll (pipe, response + "\n");
                }
                if (keep && buffer.size() > maxLineBytes)
                {
                    writeAll (pipe, errorLine ("bad_request", "request line too long (max 1 MiB)"));
                    keep = false;
                }
            }

            if (ov.hEvent != nullptr)
                CloseHandle (ov.hEvent);
            closePipe (pipe);
            --clients;
            *done = true;
        };

        OVERLAPPED ov {};
        ov.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        HANDLE pending = os->firstInstance;
        os->firstInstance = INVALID_HANDLE_VALUE;

        while (ov.hEvent != nullptr)
        {
            // Finished clients: join and forget them.
            auto& list = os->clientThreads;
            for (auto it = list.begin(); it != list.end();)
                if (*it->done) { it->thread.join(); it = list.erase (it); }
                else ++it;

            if (pending == INVALID_HANDLE_VALUE)
                pending = createInstance (os->name, os->security, false);
            if (pending == INVALID_HANDLE_VALUE)
            {
                if (WaitForSingleObject (os->stopEvent, 1000) == WAIT_OBJECT_0)
                    break;
                continue;   // e.g. out of resources; try again
            }

            ResetEvent (ov.hEvent);
            bool connected = ConnectNamedPipe (pending, &ov) != 0;
            const auto status = connected ? (DWORD) ERROR_SUCCESS : GetLastError();
            connected = connected || status == ERROR_PIPE_CONNECTED;
            if (! connected && status == ERROR_IO_PENDING)
            {
                const HANDLE waits[] { os->stopEvent, ov.hEvent };
                if (WaitForMultipleObjects (2, waits, FALSE, INFINITE) == WAIT_OBJECT_0)
                {
                    DWORD ignored = 0;
                    CancelIoEx (pending, &ov);
                    GetOverlappedResult (pending, &ov, &ignored, TRUE);
                    CloseHandle (pending);
                    pending = INVALID_HANDLE_VALUE;
                    break;
                }
                DWORD ignored = 0;
                connected = GetOverlappedResult (pending, &ov, &ignored, FALSE) != 0;
            }
            if (! connected)
            {
                CloseHandle (pending);
                pending = INVALID_HANDLE_VALUE;
                continue;
            }

            const HANDLE pipe = pending;
            pending = INVALID_HANDLE_VALUE;
            const auto sid = clientSid (pipe);
            const bool sameUser = sid.empty() || sid == os->userSid;
            if (! sameUser || clients.load() >= maxClients)
            {
                writeAll (pipe, errorLine ("refused", sameUser ? "too many connections" : "not allowed"));
                closePipe (pipe);
                continue;
            }

            ++clients;
            auto done = std::make_shared<std::atomic<bool>> (false);
            list.push_back ({ std::thread (serve, pipe, done), done });
        }

        if (pending != INVALID_HANDLE_VALUE)
            CloseHandle (pending);
        if (ov.hEvent != nullptr)
            CloseHandle (ov.hEvent);

        // stop(): the stop event ends every client's wait.
        SetEvent (os->stopEvent);
        for (auto& c : os->clientThreads)
            c.thread.join();
        os->clientThreads.clear();
        clients = 0;
    }
}
