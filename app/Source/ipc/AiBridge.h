#pragma once

#include <beatforge/api/CommandApi.h>
#include <beatforge/ipc/IpcServer.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>

namespace beatforge
{
    // Connects the MCP server (a separate process) to the app: the IPC socket
    // on one side, the command API on the other. Requests arrive on the IPC
    // thread and run on the message thread, like every other edit; if the app
    // does not answer within `timeout` the caller gets a "timeout" error and the
    // request is guaranteed not to run later.
    class AiBridge
    {
    public:
        AiBridge (project::Session&, audio::AudioEngine&, api::CommandApi::Environment);
        ~AiBridge();

        bool start (std::string& error);   // on IpcServer::defaultPath()
        juce::String statusText() const;   // for the status bar

        static constexpr int timeoutMs = 10000;

    private:
        std::string handle (const std::string& line);

        std::shared_ptr<api::CommandApi> api;
        std::unique_ptr<ipc::IpcServer> server;
        std::atomic<bool> closing { false };
        juce::String failure;
    };
}
