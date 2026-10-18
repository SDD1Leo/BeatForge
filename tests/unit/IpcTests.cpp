#include <beatforge/api/CommandApi.h>
#include <beatforge/ipc/IpcServer.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "TestSupport.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cstring>
 #include <sys/socket.h>
 #include <sys/stat.h>
 #include <sys/un.h>
 #include <unistd.h>
#endif

using namespace beatforge;
using api::Json;
namespace fs = std::filesystem;

namespace
{
    fs::path testDir() { return fs::temp_directory_path() / ("beatforge-ipc-test-" + std::to_string (test::processId())); }

   #ifdef _WIN32
    fs::path testEndpoint() { return fs::path (L"\\\\.\\pipe\\beatforge-ipc-test-" + std::to_wstring (test::processId())); }

    // A named-pipe client, as the MCP server connects on Windows.
    struct Client
    {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        std::string pending;

        explicit Client (const fs::path& path)
        {
            for (int attempt = 0; attempt < 50 && pipe == INVALID_HANDLE_VALUE; ++attempt)
            {
                pipe = CreateFileW (path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (pipe == INVALID_HANDLE_VALUE)
                    WaitNamedPipeW (path.wstring().c_str(), 100);   // all instances busy: the server is between accepts
            }
            REQUIRE (pipe != INVALID_HANDLE_VALUE);
        }
        ~Client() { CloseHandle (pipe); }

        void send (const std::string& s)
        {
            DWORD written = 0;
            REQUIRE (WriteFile (pipe, s.data(), (DWORD) s.size(), &written, nullptr));
            REQUIRE (written == s.size());
        }

        std::string line()
        {
            for (;;)
            {
                if (const auto nl = pending.find ('\n'); nl != std::string::npos)
                {
                    auto l = pending.substr (0, nl);
                    pending.erase (0, nl + 1);
                    return l;
                }
                char buf[4096];
                DWORD n = 0;
                if (! ReadFile (pipe, buf, sizeof (buf), &n, nullptr) || n == 0) return {};
                pending.append (buf, n);
            }
        }
    };
   #else
    fs::path testEndpoint() { return testDir() / "daw.sock"; }

    struct Client
    {
        int fd = -1;
        std::string pending;

        explicit Client (const fs::path& path)
        {
            fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un addr {};
            addr.sun_family = AF_UNIX;
            std::strncpy (addr.sun_path, path.c_str(), sizeof (addr.sun_path) - 1);
            REQUIRE (::connect (fd, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) == 0);
        }
        ~Client() { ::close (fd); }

        void send (const std::string& s) { REQUIRE (::send (fd, s.data(), s.size(), MSG_NOSIGNAL) == (ssize_t) s.size()); }

        std::string line()
        {
            for (;;)
            {
                if (const auto nl = pending.find ('\n'); nl != std::string::npos)
                {
                    auto l = pending.substr (0, nl);
                    pending.erase (0, nl + 1);
                    return l;
                }
                char buf[4096];
                const auto n = ::recv (fd, buf, sizeof (buf), 0);
                if (n <= 0) return {};
                pending.append (buf, (size_t) n);
            }
        }
    };
   #endif

    struct ServerRig
    {
        audio::AudioEngine engine;
        project::Session session { engine };
        api::CommandApi api { session, engine, {} };
        ipc::IpcServer server { [this] (const std::string& l)
        {
            return ipc::handleRequestLine (l, [this] (const std::string& op, const Json& args) { return api.execute (op, args); });
        } };
        fs::path path = testEndpoint();

        ServerRig()
        {
            fs::remove_all (testDir());
            std::string error;
            REQUIRE (server.start (path, error));
        }
        ~ServerRig() { server.stop(); fs::remove_all (testDir()); }
    };
}

TEST_CASE ("Requests over the socket reach the command API and are answered in order", "[ipc]")
{
    ServerRig rig;
    Client c (rig.path);
    c.send (R"({"id": 1, "op": "hello"})" "\n" R"({"id": "two", "op": "set_tempo", "args": {"bpm": 99}})" "\n");

    const auto hello = Json::parse (c.line());
    REQUIRE (hello["id"] == 1);
    REQUIRE (hello["ok"] == true);
    REQUIRE (hello["result"]["app"] == "BeatForge");
    REQUIRE (hello["result"]["protocol"] == 1);

    const auto tempo = Json::parse (c.line());
    REQUIRE (tempo["id"] == "two");
    REQUIRE (tempo["result"]["bpm"] == 99.0);
    REQUIRE (rig.session.getProject().tempo == 99.0);   // the real project changed

    // Broken requests get structured errors; the connection keeps working.
    c.send ("this is not json\n{\"id\": 3}\n{\"id\": 4, \"op\": \"set_tempo\", \"args\": {\"bpm\": 1000}}\n");
    REQUIRE (Json::parse (c.line())["error"]["code"] == "bad_request");
    const auto noOp = Json::parse (c.line());
    REQUIRE (noOp["id"] == 3);
    REQUIRE (noOp["error"]["code"] == "bad_request");
    const auto range = Json::parse (c.line());
    REQUIRE (range["id"] == 4);
    REQUIRE (range["error"]["code"] == "invalid_argument");
    REQUIRE (rig.server.getClientCount() == 1);
}

#ifdef _WIN32
TEST_CASE ("The pipe is never taken over by a second BeatForge", "[ipc][safety]")
{
    ServerRig rig;

    ipc::IpcServer second ([] (const std::string&) { return std::string ("{}"); });
    std::string error;
    REQUIRE_FALSE (second.start (rig.path, error));
    REQUIRE (error.find ("another BeatForge") != std::string::npos);

    // Once the first one stops, the name is free again.
    rig.server.stop();
    REQUIRE (second.start (rig.path, error));
    {
        Client c (rig.path);
        c.send ("{\"id\": 1, \"op\": \"x\"}\n");
        REQUIRE (Json::parse (c.line()) == Json::object());
    }
    second.stop();

    // Only pipe names are accepted on Windows.
    ipc::IpcServer third ([] (const std::string&) { return std::string ("{}"); });
    REQUIRE_FALSE (third.start (testDir() / "daw.sock", error));
    REQUIRE (error.find ("not a pipe name") != std::string::npos);
}

#else
TEST_CASE ("The socket is private and never taken over", "[ipc][safety]")
{
    ServerRig rig;
    struct stat st {};
    REQUIRE (::stat (rig.path.c_str(), &st) == 0);
    REQUIRE ((st.st_mode & 0777) == 0600);
    REQUIRE (::stat (rig.path.parent_path().c_str(), &st) == 0);
    REQUIRE ((st.st_mode & 0777) == 0700);

    // A second instance must not steal a live socket.
    ipc::IpcServer second ([] (const std::string&) { return std::string ("{}"); });
    std::string error;
    REQUIRE_FALSE (second.start (rig.path, error));
    REQUIRE (error.find ("another BeatForge") != std::string::npos);

    // stop() removes the socket; a stale file left by a crash is replaced.
    rig.server.stop();
    REQUIRE_FALSE (fs::exists (rig.path));
    {
        const int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        std::strncpy (addr.sun_path, rig.path.c_str(), sizeof (addr.sun_path) - 1);
        REQUIRE (::bind (fd, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) == 0);
        ::close (fd);   // a socket file nobody listens on
    }
    REQUIRE (second.start (rig.path, error));
    second.stop();

    // A folder others can read is refused.
    const auto open = testDir() / "public";
    fs::create_directories (open);
    ::chmod (open.c_str(), 0755);
    ipc::IpcServer third ([] (const std::string&) { return std::string ("{}"); });
    REQUIRE_FALSE (third.start (open / "daw.sock", error));
    REQUIRE (error.find ("private") != std::string::npos);
}
#endif

TEST_CASE ("Oversized requests are refused and the client is disconnected", "[ipc]")
{
    ServerRig rig;
    Client c (rig.path);
    c.send (std::string (ipc::IpcServer::maxLineBytes + 10, 'x'));
    REQUIRE (Json::parse (c.line())["error"]["message"].get<std::string>().find ("too long") != std::string::npos);
    REQUIRE (c.line().empty());   // closed
}
