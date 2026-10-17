#include "beatforge/project/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace beatforge::project
{
    using json = nlohmann::ordered_json;

    namespace
    {
        constexpr std::uintmax_t maxFileBytes = 20 * 1024 * 1024;

        const char* soundId (int lane)
        {
            static const char* ids[] { "kick", "snare", "closedHat", "openHat", "clap" };
            return ids[lane];
        }

        int laneForSoundId (const std::string& id)
        {
            for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
                if (id == soundId (i))
                    return i;
            return -1;
        }

        std::string stepsToString (const std::array<bool, audio::DrumMachine::numSteps>& steps)
        {
            std::string s;
            for (auto on : steps)
                s += on ? 'x' : '.';
            return s;
        }

        // Keys of `object` not in `known`, as a JSON object string.
        std::string extrasOf (const json& object, std::initializer_list<const char*> known)
        {
            const std::set<std::string> knownKeys (known.begin(), known.end());
            json extra = json::object();

            for (auto it = object.begin(); it != object.end(); ++it)
                if (! knownKeys.contains (it.key()))
                    extra[it.key()] = it.value();

            return extra.dump();
        }

        json parseOr (const std::string& text, json fallback)
        {
            auto parsed = json::parse (text, nullptr, false);
            return parsed.is_discarded() ? fallback : parsed;
        }

        // Reads `parent[key]` as a string, recording a warning when it is unusable.
        std::string readName (const json& parent, const char* key, const std::string& fallback, size_t maxLength,
                              const std::string& what, std::vector<std::string>& warnings)
        {
            if (! parent.contains (key))
            {
                warnings.push_back (what + " was missing; using \"" + fallback + "\".");
                return fallback;
            }

            const auto& v = parent[key];
            if (! v.is_string() || v.get<std::string>().empty())
            {
                warnings.push_back (what + " was not a valid name; using \"" + fallback + "\".");
                return fallback;
            }

            auto name = v.get<std::string>();
            if (name.size() > maxLength)
            {
                warnings.push_back (what + " was longer than " + std::to_string (maxLength) + " characters and was shortened.");
                name.resize (maxLength);
            }
            return name;
        }

        bool readBool (const json& parent, const char* key, bool fallback, const std::string& what,
                       std::vector<std::string>& warnings)
        {
            if (! parent.contains (key))
                return fallback;

            if (! parent[key].is_boolean())
            {
                warnings.push_back (what + " was not true/false; using " + (fallback ? "true" : "false") + ".");
                return fallback;
            }
            return parent[key].get<bool>();
        }

        DrumLane readLane (const json& j, int lane, std::vector<std::string>& warnings)
        {
            const auto defaultName = std::string (audio::drumSoundName ((audio::DrumSound) lane));
            const auto what = "Drum lane \"" + defaultName + "\"";

            DrumLane result;
            result.name = readName (j, "name", defaultName, Project::maxLaneNameLength, what + " name", warnings);
            result.muted = readBool (j, "muted", false, what + " mute", warnings);
            result.soloed = readBool (j, "soloed", false, what + " solo", warnings);

            if (j.contains ("gainDb"))
            {
                if (j["gainDb"].is_number() && std::isfinite (j["gainDb"].get<double>()))
                {
                    const auto db = (float) j["gainDb"].get<double>();
                    result.gainDb = std::clamp (db, audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb);
                    if (result.gainDb != db)
                        warnings.push_back (what + " volume was outside -60..+6 dB and was limited.");
                }
                else
                {
                    warnings.push_back (what + " volume was not a number; using 0 dB.");
                }
            }

            json extra = parseOr (extrasOf (j, { "sound", "name", "gainDb", "muted", "soloed", "steps" }), json::object());

            if (j.contains ("steps"))
            {
                if (! j["steps"].is_string())
                {
                    warnings.push_back (what + " steps were unreadable; the lane is empty. The original value is kept in the file.");
                    extra["unreadableSteps"] = j["steps"];
                }
                else
                {
                    const auto steps = j["steps"].get<std::string>();
                    bool strange = false;

                    for (size_t i = 0; i < steps.size() && i < result.steps.size(); ++i)
                    {
                        const char c = steps[i];
                        result.steps[i] = c == 'x' || c == 'X';
                        strange |= ! (c == 'x' || c == 'X' || c == '.' || c == '-');
                    }

                    if (strange)
                        warnings.push_back (what + " steps contained characters other than 'x' and '.'; those steps are off.");

                    if (steps.size() > result.steps.size())
                    {
                        warnings.push_back (what + " has " + std::to_string (steps.size())
                                            + " steps; BeatForge plays the first 16 and keeps the rest in the file.");
                        extra["stepsAfter16"] = steps.substr (result.steps.size());
                    }
                }
            }

            result.extra = extra.dump();
            return result;
        }
    }

    namespace
    {
        const char* waveNames[] { "sine", "saw", "square", "triangle" };

        json paramsToJson (const Project& p, audio::InstrumentType type)
        {
            json out = parseOr (p.extraInstrumentParams[(size_t) type], json::object());
            if (! out.is_object())
                out = json::object();

            for (int i = 0; i < audio::numParams; ++i)
            {
                const auto param = (audio::Param) i;
                const auto value = p.instrumentParams[(size_t) type][(size_t) i];
                const auto id = std::string (audio::paramSpec (param).id);
                if (param == audio::Param::wave)
                    out[id] = waveNames[std::clamp ((int) value, 0, 3)];
                else
                    out[id] = value;
            }
            return out;
        }

        void readParams (const json& source, Project& p, audio::InstrumentType type, std::vector<std::string>& warnings)
        {
            const auto name = std::string (audio::instrumentName (type));
            std::vector<const char*> known;
            int corrected = 0;

            for (int i = 0; i < audio::numParams; ++i)
            {
                const auto param = (audio::Param) i;
                const auto& spec = audio::paramSpec (param);
                const auto id = std::string (spec.id);
                known.push_back (spec.id.data());

                if (! source.contains (id))
                    continue;   // keeps the default

                const auto& v = source[id];
                float value = 0.0f;

                if (param == audio::Param::wave)
                {
                    const auto* found = v.is_string() ? std::find_if (std::begin (waveNames), std::end (waveNames),
                                                                      [&] (const char* w) { return v.get<std::string>() == w; })
                                                      : std::end (waveNames);
                    if (found == std::end (waveNames)) { ++corrected; continue; }
                    value = (float) (found - std::begin (waveNames));
                }
                else
                {
                    if (! v.is_number() || ! std::isfinite (v.get<double>())) { ++corrected; continue; }
                    value = (float) v.get<double>();
                    const auto clamped = std::clamp (value, spec.min, spec.max);
                    if (clamped != value) ++corrected;
                    value = clamped;
                }

                p.instrumentParams[(size_t) type][(size_t) i] = value;
            }

            if (corrected > 0)
                warnings.push_back (std::to_string (corrected) + " " + name + " setting(s) were out of range or unreadable and were corrected.");

            // Unknown keys (settings from a newer version) are kept.
            json extra = json::object();
            for (auto it = source.begin(); it != source.end(); ++it)
                if (std::find_if (known.begin(), known.end(), [&] (const char* k) { return it.key() == k; }) == known.end())
                    extra[it.key()] = it.value();
            p.extraInstrumentParams[(size_t) type] = extra.dump();
        }

        void readSynthTrack (const json& track, Project& p, std::vector<std::string>& warnings)
        {
            p.synthTrackName = readName (track, "name", "Synth", Project::maxNameLength, "Synth track name", warnings);
            p.extraSynthTrack = extrasOf (track, { "type", "instrument", "name", "clip", "params" });

            // "basicSynth" (0.5) became Nebula Poly in 0.6.
            p.instrument = track["instrument"] == "monoBass" ? audio::InstrumentType::monoBass : audio::InstrumentType::polySynth;

            if (track.contains ("params"))
            {
                const auto& params = track["params"];
                if (params.is_object())
                {
                    for (int t = 0; t < audio::numInstrumentTypes; ++t)
                    {
                        const auto id = std::string (audio::instrumentId ((audio::InstrumentType) t));
                        if (params.contains (id) && params[id].is_object())
                            readParams (params[id], p, (audio::InstrumentType) t, warnings);
                    }
                    auto extra = parseOr (p.extraSynthTrack, json::object());
                    json unknownParams = json::object();
                    for (auto it = params.begin(); it != params.end(); ++it)
                        if (it.key() != "polySynth" && it.key() != "monoBass")
                            unknownParams[it.key()] = it.value();
                    if (! unknownParams.empty())
                    {
                        extra["otherInstrumentParams"] = unknownParams;
                        p.extraSynthTrack = extra.dump();
                    }
                }
                else
                {
                    warnings.push_back ("The instrument settings were unreadable; using defaults.");
                }
            }

            if (! track.contains ("clip"))
                return;   // a synth track without a clip yet

            const auto& clip = track["clip"];
            if (! clip.is_object())
            {
                warnings.push_back ("The synth clip was unreadable; it is kept in the file but not shown.");
                auto extra = parseOr (p.extraSynthTrack, json::object());
                extra["unreadableClip"] = clip;
                p.extraSynthTrack = extra.dump();
                return;
            }

            midi::Clip result;
            if (clip.contains ("lengthTicks") && clip["lengthTicks"].is_number_integer() && clip["lengthTicks"].get<std::int64_t>() > 0
                && clip["lengthTicks"].get<std::int64_t>() <= 32 * 3840)
                result.length = clip["lengthTicks"].get<std::int64_t>();
            else
                warnings.push_back ("The synth clip length was missing or invalid; using 4 bars.");

            p.extraClip = extrasOf (clip, { "lengthTicks", "notes" });
            json invalid = json::array();
            midi::NoteId nextId = 1;
            int corrected = 0;

            if (clip.contains ("notes") && clip["notes"].is_array())
            {
                for (auto& n : clip["notes"])
                {
                    const bool readable = n.is_object() && n.contains ("pitch") && n.contains ("start") && n.contains ("length")
                                       && n["pitch"].is_number_integer() && n["start"].is_number_integer()
                                       && n["length"].is_number_integer()
                                       && n["start"].get<std::int64_t>() >= 0 && n["length"].get<std::int64_t>() >= 1;
                    if (! readable)
                    {
                        invalid.push_back (n);
                        continue;
                    }

                    midi::Note note;
                    note.id = nextId++;
                    note.pitch = n["pitch"].get<int>();
                    note.start = n["start"].get<std::int64_t>();
                    note.length = n["length"].get<std::int64_t>();
                    note.velocity = n.contains ("velocity") && n["velocity"].is_number_integer() ? n["velocity"].get<int>() : 100;

                    const auto clampedPitch = std::clamp (note.pitch, midi::minPitch, midi::maxPitch);
                    const auto clampedVelocity = std::clamp (note.velocity, midi::minVelocity, midi::maxVelocity);
                    if (clampedPitch != note.pitch || clampedVelocity != note.velocity)
                        ++corrected;
                    note.pitch = clampedPitch;
                    note.velocity = clampedVelocity;

                    const auto extra = extrasOf (n, { "pitch", "velocity", "start", "length" });
                    if (extra != "{}")
                        p.noteExtra[note.id] = extra;

                    result.notes.push_back (note);
                }
            }

            if (corrected > 0)
                warnings.push_back (std::to_string (corrected) + " synth note(s) had a pitch or velocity out of range and were limited.");
            if (! invalid.empty())
                warnings.push_back (std::to_string (invalid.size()) + " synth note(s) were unreadable; they are kept in the file but not shown.");

            p.invalidNotes = invalid.dump();
            p.synthClip = std::move (result);
        }
    }

    //==============================================================================
    std::string toJson (const Project& p, const std::string& appVersion)
    {
        json j = parseOr (p.extraTopLevel, json::object());
        if (! j.is_object())
            j = json::object();

        j["format"] = formatName;
        j["version"] = currentVersion;
        j["savedBy"] = "BeatForge " + appVersion;
        j["name"] = p.name;
        j["tempo"] = p.tempo;
        j["timeSignature"] = { { "numerator", p.timeSignature.numerator }, { "denominator", p.timeSignature.denominator } };
        j["metronome"] = p.metronomeEnabled;
        j["loop"] = { { "enabled", p.loopEnabled }, { "startBar", 1 }, { "bars", p.loopBars } };

        json track = parseOr (p.extraDrumTrack, json::object());
        if (! track.is_object())
            track = json::object();

        track["type"] = "drumMachine";
        track["name"] = p.drumTrackName;
        json lanes = json::array();

        for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
        {
            const auto& lane = p.lanes[(size_t) i];
            json l = { { "sound", soundId (i) }, { "name", lane.name }, { "gainDb", lane.gainDb },
                       { "muted", lane.muted }, { "soloed", lane.soloed }, { "steps", stepsToString (lane.steps) } };

            auto extra = parseOr (lane.extra, json::object());
            if (extra.is_object())
                for (auto it = extra.begin(); it != extra.end(); ++it)
                    if (! l.contains (it.key()))
                        l[it.key()] = it.value();

            lanes.push_back (l);
        }

        for (auto& extraLane : parseOr (p.extraLanes, json::array()))
            lanes.push_back (extraLane);

        track["lanes"] = lanes;

        json tracks = json::array ({ track });

        // The synth track is always written: its instrument settings matter even
        // before a clip exists.
        {
            json synth = parseOr (p.extraSynthTrack, json::object());
            if (! synth.is_object())
                synth = json::object();

            json params = { { "polySynth", paramsToJson (p, audio::InstrumentType::polySynth) },
                            { "monoBass", paramsToJson (p, audio::InstrumentType::monoBass) } };
            if (synth.contains ("otherInstrumentParams"))
            {
                // Settings of instruments this version does not know go back where they came from.
                for (auto it = synth["otherInstrumentParams"].begin(); it != synth["otherInstrumentParams"].end(); ++it)
                    params[it.key()] = it.value();
                synth.erase ("otherInstrumentParams");
            }

            synth["type"] = "instrument";
            synth["instrument"] = std::string (audio::instrumentId (p.instrument));
            synth["name"] = p.synthTrackName;
            synth["params"] = params;

            json clip = parseOr (p.extraClip, json::object());
            if (! clip.is_object())
                clip = json::object();

            if (p.synthClip)
            {
                clip["lengthTicks"] = p.synthClip->length;
                json notes = json::array();
                for (auto& n : p.synthClip->notes)
                {
                    json note = { { "pitch", n.pitch }, { "velocity", n.velocity }, { "start", n.start }, { "length", n.length } };
                    if (auto it = p.noteExtra.find (n.id); it != p.noteExtra.end())
                    {
                        auto extra = parseOr (it->second, json::object());
                        if (extra.is_object())
                            for (auto e = extra.begin(); e != extra.end(); ++e)
                                if (! note.contains (e.key()))
                                    note[e.key()] = e.value();
                    }
                    notes.push_back (note);
                }
                for (auto& raw : parseOr (p.invalidNotes, json::array()))
                    notes.push_back (raw);

                clip["notes"] = notes;
                synth["clip"] = clip;
            }
            tracks.push_back (synth);
        }
        for (auto& extraTrack : parseOr (p.extraTracks, json::array()))
            tracks.push_back (extraTrack);

        j["tracks"] = tracks;
        return j.dump (2) + "\n";
    }

    LoadResult fromJson (const std::string& text)
    {
        LoadResult result;

        json j;
        try
        {
            j = json::parse (text);
        }
        catch (const json::parse_error& e)
        {
            result.error = "The file is not valid JSON (error near byte " + std::to_string (e.byte) + "), so it cannot be a "
                           "BeatForge project, or it is damaged.";
            return result;
        }

        if (! j.is_object() || ! j.contains ("format") || j["format"] != formatName)
        {
            result.error = "This file is not a BeatForge project (it has no \"format\": \"beatforge-project\" marker).";
            return result;
        }

        if (! j.contains ("version") || ! j["version"].is_number_integer() || j["version"].get<int>() < 1)
        {
            result.error = "This BeatForge project has no valid format version, so it cannot be read safely.";
            return result;
        }

        const int version = j["version"].get<int>();
        if (version > currentVersion)
        {
            result.error = "This project was saved by a newer BeatForge (file format version " + std::to_string (version)
                         + "). This version reads up to version " + std::to_string (currentVersion)
                         + ". The file was not changed.";
            return result;
        }

        auto& warnings = result.warnings;
        Project p;

        p.name = readName (j, "name", "Untitled", Project::maxNameLength, "Project name", warnings);

        if (j.contains ("tempo") && j["tempo"].is_number() && std::isfinite (j["tempo"].get<double>()))
        {
            const auto bpm = j["tempo"].get<double>();
            p.tempo = time::clampTempo (bpm);
            if (p.tempo != bpm)
                warnings.push_back ("Tempo " + std::to_string ((int) std::lround (bpm)) + " BPM is outside 40-240 and was limited to "
                                    + std::to_string ((int) std::lround (p.tempo)) + " BPM.");
        }
        else
        {
            warnings.push_back ("Tempo was missing or not a number; using 120 BPM.");
        }

        const auto& sig = j.contains ("timeSignature") ? j["timeSignature"] : json();
        if (sig.is_object() && sig.contains ("numerator") && sig.contains ("denominator")
            && sig["numerator"].is_number_integer() && sig["denominator"].is_number_integer()
            && time::TimeSignature { sig["numerator"].get<int>(), sig["denominator"].get<int>() }.isValid())
        {
            p.timeSignature = { sig["numerator"].get<int>(), sig["denominator"].get<int>() };
        }
        else
        {
            warnings.push_back ("Time signature was missing or invalid; using 4/4.");
        }

        p.metronomeEnabled = readBool (j, "metronome", false, "Metronome setting", warnings);

        if (j.contains ("loop"))
        {
            const auto& loop = j["loop"];
            if (loop.is_object())
            {
                p.loopEnabled = readBool (loop, "enabled", false, "Loop setting", warnings);
                if (loop.contains ("bars"))
                {
                    if (loop["bars"].is_number_integer() && Project::isValidLoopBars (loop["bars"].get<int>()))
                        p.loopBars = loop["bars"].get<int>();
                    else
                        warnings.push_back ("Loop length must be 1, 2, 4 or 8 bars; using 4.");
                }
            }
            else
            {
                warnings.push_back ("Loop settings were unreadable; loop is off.");
            }
        }

        p.extraTopLevel = extrasOf (j, { "format", "version", "savedBy", "name", "tempo", "timeSignature",
                                         "metronome", "loop", "tracks" });

        // Tracks: the first drum machine is ours; anything else is kept verbatim.
        json extraTracks = json::array();
        bool haveDrums = false, haveSynth = false;

        if (j.contains ("tracks") && j["tracks"].is_array())
        {
            for (auto& track : j["tracks"])
            {
                const bool isDrums = track.is_object() && track.contains ("type") && track["type"] == "drumMachine";
                const bool isSynth = track.is_object() && track.contains ("type") && track["type"] == "instrument"
                                  && track.contains ("instrument")
                                  && (track["instrument"] == "basicSynth" || track["instrument"] == "polySynth"
                                      || track["instrument"] == "monoBass");

                if (isSynth && ! haveSynth)
                {
                    haveSynth = true;
                    readSynthTrack (track, p, warnings);
                    continue;
                }

                if (haveDrums || ! isDrums)
                {
                    extraTracks.push_back (track);
                    warnings.push_back ("A track this version cannot play was kept in the file but is not shown.");
                    continue;
                }

                haveDrums = true;
                p.drumTrackName = readName (track, "name", "Drums", Project::maxNameLength, "Drum track name", warnings);
                p.extraDrumTrack = extrasOf (track, { "type", "name", "lanes" });

                std::array<bool, audio::DrumMachine::numLanes> seen {};
                json extraLanes = json::array();

                if (track.contains ("lanes") && track["lanes"].is_array())
                {
                    for (auto& lane : track["lanes"])
                    {
                        const int index = lane.is_object() && lane.contains ("sound") && lane["sound"].is_string()
                                              ? laneForSoundId (lane["sound"].get<std::string>()) : -1;

                        if (index < 0 || seen[(size_t) index])
                        {
                            extraLanes.push_back (lane);
                            warnings.push_back (index < 0 ? "A drum lane with an unknown sound was kept in the file but cannot play."
                                                          : "A duplicate drum lane was kept in the file but is not shown.");
                            continue;
                        }

                        seen[(size_t) index] = true;
                        p.lanes[(size_t) index] = readLane (lane, index, warnings);
                    }
                }
                else
                {
                    warnings.push_back ("The drum track had no lanes; starting with an empty pattern.");
                }

                p.extraLanes = extraLanes.dump();
            }
        }

        if (! haveDrums)
            warnings.push_back ("The project had no drum machine track; starting with an empty pattern.");

        p.extraTracks = extraTracks.dump();
        result.project = std::move (p);
        return result;
    }

    //==============================================================================
    LoadResult loadFile (const std::filesystem::path& path)
    {
        LoadResult result;
        std::error_code ec;

        if (! std::filesystem::is_regular_file (path, ec))
        {
            result.error = "The file " + path.string() + " does not exist or is not a regular file.";
            return result;
        }

        if (std::filesystem::file_size (path, ec) > maxFileBytes)
        {
            result.error = "The file is larger than 20 MB, which is far too big for a BeatForge project.";
            return result;
        }

        std::ifstream in (path, std::ios::binary);
        if (! in)
        {
            result.error = "The file could not be opened for reading (check permissions).";
            return result;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();
        return fromJson (buffer.str());
    }

    SaveResult saveFile (const Project& p, const std::filesystem::path& path, const std::string& appVersion)
    {
        const auto text = toJson (p, appVersion);
        auto temp = path;
        temp += ".tmp";

        {
            errno = 0;
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            if (! out)
                return { "Could not write to " + path.parent_path().string() + ": " + std::strerror (errno)
                         + ". Check that the folder exists and is writable." };

            out << text;
            out.flush();
            if (! out)
            {
                std::error_code ignored;
                std::filesystem::remove (temp, ignored);
                return { "Writing the project failed (the disk may be full). The previous file was not changed." };
            }
        }

        std::error_code ec;
        std::filesystem::rename (temp, path, ec);
        if (ec)
        {
            std::error_code ignored;
            std::filesystem::remove (temp, ignored);
            return { "Could not replace " + path.filename().string() + ": " + ec.message() + ". The previous file was not changed." };
        }

        return {};
    }
}
