#pragma once

#include "beatforge/audio/AudioEngine.h"
#include "beatforge/project/AudioExport.h"
#include "beatforge/project/Session.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beatforge::api
{
    using Json = nlohmann::json;

    // The DAW command API: named operations with JSON arguments and results,
    // used by the MCP bridge today and an embedded assistant later. Every
    // change goes through the Session (validation, undo, engine, UI refresh);
    // nothing here touches the project or the engine behind its back.
    //
    // Message-thread only (like the Session). Export jobs run on their own
    // threads and are polled with get_export_status.
    //
    // Safety:
    //  - files are read and written only inside `allowedFolders` (relative
    //    names resolve into the first one); no shell, no other paths;
    //  - operations that lose work not covered by undo (discarding unsaved
    //    changes, overwriting a file) fail with "confirmation_required" unless
    //    called with confirm / overwrite = true.
    class CommandApi
    {
    public:
        struct Environment
        {
            std::vector<std::filesystem::path> allowedFolders;   // first = default
            std::string appVersion;
            std::function<bool()> audioAvailable;                // can play() be heard?
            std::function<void (project::TrackId, project::ClipId)> reveal;   // show in the UI
            std::function<void (const std::filesystem::path&)> fileSaved;     // e.g. recent files
        };

        CommandApi (project::Session&, audio::AudioEngine&, Environment);
        ~CommandApi();   // cancels and joins running exports

        // {"ok": true, "result": ...} or {"ok": false, "error": {"code", "message"}}.
        Json execute (const std::string& op, const Json& args);

        static const std::vector<std::string>& operations();

        // Errors returned to the caller (thrown inside an operation).
        struct Error
        {
            std::string code, message;
        };

    private:
        struct ExportJob;

        Json run (const std::string& op, const Json& args);
        Json projectState() const;
        Json transportState() const;
        Json trackJson (const project::Track&) const;
        Json clipJson (const project::Track&, const project::Clip&, bool withContent) const;

        const project::Track& trackArg (const Json& args, const char* key = "track") const;
        const project::Clip& clipArg (const Json& args, const char* key = "clip") const;
        std::filesystem::path fileArg (const Json& args, const char* extension, bool forWriting) const;
        void requireConfirm (const Json& args, const char* key, const std::string& why) const;

        project::Session& session;
        audio::AudioEngine& engine;
        Environment env;

        std::map<std::string, std::unique_ptr<ExportJob>> jobs;
        int nextJob = 1;
    };

    // Parses note names like "C4" (= 60), "F#3", "Bb2"; -1 if invalid.
    int parseNoteName (const std::string&) noexcept;
    std::string noteName (int pitch);
}
