#include "ipc/AiBridge.h"

#include <chrono>
#include <future>

namespace beatforge
{
    namespace
    {
        // Who wins for one MCP call: the task starting, or the waiter giving up.
        enum CallState : int { pending, running, abandoned };
    }

    AiBridge::AiBridge (project::Session& session, audio::AudioEngine& engine, api::CommandApi::Environment env)
        : api (std::make_shared<api::CommandApi> (session, engine, std::move (env))),
          server (std::make_unique<ipc::IpcServer> ([this] (const std::string& line) { return handle (line); }))
    {
    }

    AiBridge::~AiBridge()
    {
        closing = true;      // a waiting request gives up at once
        server->stop();      // joins the IPC thread
        api.reset();         // message thread: no request can be running now
    }

    bool AiBridge::start (std::string& error)
    {
        if (server->start (ipc::IpcServer::defaultPath(), error))
            return true;
        failure = error;
        return false;
    }

    juce::String AiBridge::statusText() const
    {
        if (! server->isRunning())
            return "MCP off" + (failure.isNotEmpty() ? " (" + failure + ")" : juce::String());
        const auto clients = server->getClientCount();
        return clients == 0 ? juce::String ("MCP ready")
                            : "MCP connected (" + juce::String ((int) server->getRequestCount()) + " requests)";
    }

    std::string AiBridge::handle (const std::string& line)
    {
        return ipc::handleRequestLine (line, [this] (const std::string& op, const api::Json& args) -> api::Json
        {
            // Run on the message thread; `state` (a CallState) decides who wins.
            struct Call
            {
                std::atomic<int> state { pending };
                std::promise<api::Json> result;
            };
            auto call = std::make_shared<Call>();
            auto future = call->result.get_future();
            std::weak_ptr<api::CommandApi> weakApi = api;

            juce::MessageManager::callAsync ([call, weakApi, op, args]
            {
                int expected = pending;
                if (! call->state.compare_exchange_strong (expected, running))
                    return;   // the caller already gave up
                if (auto a = weakApi.lock())
                    call->result.set_value (a->execute (op, args));
                else
                    call->result.set_value ({ { "ok", false }, { "error", { { "code", "closing" }, { "message", "BeatForge is closing" } } } });
            });

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
            while (future.wait_for (std::chrono::milliseconds (25)) != std::future_status::ready)
            {
                if (closing || std::chrono::steady_clock::now() > deadline)
                {
                    int expected = pending;
                    if (call->state.compare_exchange_strong (expected, abandoned))
                        return { { "ok", false }, { "error", { { "code", closing ? "closing" : "timeout" },
                                                              { "message", closing ? "BeatForge is closing"
                                                                                   : "BeatForge did not answer within 10 s; the request was not applied" } } } };
                    future.wait();   // it already started: wait for its result
                    break;
                }
            }
            return future.get();
        });
    }
}
