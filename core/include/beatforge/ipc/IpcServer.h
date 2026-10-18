#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace beatforge::ipc
{
    // Local IPC for the MCP bridge, never a network port. Access control is the
    // operating system's.
    //
    // Linux: a Unix domain socket.
    //  - It lives in a private folder (mode 0700, owned by the user):
    //    $BEATFORGE_SOCKET, else $XDG_RUNTIME_DIR/beatforge/daw.sock, else
    //    /tmp/beatforge-<uid>/daw.sock;
    //  - the socket file is mode 0600, and every connection's peer user id is
    //    checked (SO_PEERCRED) against ours.
    //
    // Windows: a named pipe, $BEATFORGE_SOCKET or \\.\pipe\beatforge-<user name>.
    //  - Its security descriptor lets only the current user open it, remote
    //    clients are rejected, and each client's user is checked against ours;
    //  - FILE_FLAG_FIRST_PIPE_INSTANCE: a second BeatForge cannot take it over.
    //
    // Protocol: one JSON object per line (UTF-8, "\n"-terminated, max 1 MiB):
    //   request   {"id": 7, "op": "set_tempo", "args": {"bpm": 128}}
    //   response  {"id": 7, "ok": true, "result": {...}}
    //             {"id": 7, "ok": false, "error": {"code": "...", "message": "..."}}
    // Requests on one connection are answered in order. The handler runs on
    // the server thread; the app forwards it to its message thread.
    class IpcServer
    {
    public:
        using Handler = std::function<std::string (const std::string& requestLine)>;

        explicit IpcServer (Handler);
        ~IpcServer();

        // Refuses (with a reason) when another live BeatForge already serves the path.
        bool start (const std::filesystem::path& socketPath, std::string& error);
        void stop();

        bool isRunning() const noexcept { return running.load(); }
        int getClientCount() const noexcept { return clients.load(); }
        std::uint64_t getRequestCount() const noexcept { return requests.load(); }
        const std::filesystem::path& getPath() const noexcept { return path; }

        static std::filesystem::path defaultPath();
        static constexpr size_t maxLineBytes = 1 << 20;
        static constexpr int maxClients = 8;

    private:
        void loop();

        struct Platform;   // the OS side: socket descriptors, or pipe handles
        std::unique_ptr<Platform> os;

        Handler handler;
        std::filesystem::path path;
        std::thread thread;
        std::atomic<bool> running { false };
        std::atomic<int> clients { 0 };
        std::atomic<std::uint64_t> requests { 0 };
    };

    // Turns one request line into one response line, calling `run (op, args)`
    // for well-formed requests. `run` returns {"ok":..., "result"|"error":...}.
    std::string handleRequestLine (const std::string& line,
                                   const std::function<nlohmann::json (const std::string& op, const nlohmann::json& args)>& run);
}
