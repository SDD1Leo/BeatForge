#include "beatforge/project/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace beatforge::project
{
    using json = nlohmann::ordered_json;

    namespace
    {
        constexpr std::uintmax_t maxFileBytes = 20 * 1024 * 1024;
        const char* waveNames[] { "sine", "saw", "square", "triangle" };

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

        json parseOr (const std::string& text, json fallback)
        {
            auto parsed = json::parse (text, nullptr, false);
            return parsed.is_discarded() ? fallback : parsed;
        }

        json objectOr (const std::string& text)
        {
            auto j = parseOr (text, json::object());
            return j.is_object() ? j : json::object();
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

        void mergeInto (json& target, const std::string& extra)
        {
            const auto e = objectOr (extra);
            for (auto it = e.begin(); it != e.end(); ++it)
                if (! target.contains (it.key()))
                    target[it.key()] = it.value();
        }

        //==============================================================================
        // Small typed readers that record a warning when a value is unusable.
        struct Reader
        {
            std::vector<std::string>& warnings;

            std::string name (const json& parent, const char* key, const std::string& fallback, size_t maxLength,
                              const std::string& what, bool required = true)
            {
                if (! parent.contains (key))
                {
                    if (required)
                        warnings.push_back (what + " was missing; using \"" + fallback + "\".");
                    return fallback;
                }
                const auto& v = parent[key];
                if (! v.is_string() || v.get<std::string>().empty())
                {
                    warnings.push_back (what + " was not a valid name; using \"" + fallback + "\".");
                    return fallback;
                }
                auto s = v.get<std::string>();
                if (s.size() > maxLength)
                {
                    warnings.push_back (what + " was longer than " + std::to_string (maxLength) + " characters and was shortened.");
                    s.resize (maxLength);
                }
                return s;
            }

            bool boolean (const json& parent, const char* key, bool fallback, const std::string& what)
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

            float number (const json& parent, const char* key, float fallback, float min, float max, const std::string& what)
            {
                if (! parent.contains (key))
                    return fallback;
                const auto& v = parent[key];
                if (! v.is_number() || ! std::isfinite (v.get<double>()))
                {
                    warnings.push_back (what + " was not a number; using the default.");
                    return fallback;
                }
                const auto value = (float) v.get<double>();
                const auto clamped = std::clamp (value, min, max);
                if (clamped != value)
                    warnings.push_back (what + " was out of range and was limited.");
                return clamped;
            }
        };

        //==============================================================================
        // Pattern steps: "x...x..." strings; anything beyond 16 steps is kept.
        void readSteps (const json& value, std::array<bool, stepsPerPattern>& steps, json& extra, const std::string& extraKey,
                        const std::string& what, std::vector<std::string>& warnings)
        {
            if (! value.is_string())
            {
                warnings.push_back (what + " steps were unreadable; they are off. The original value is kept in the file.");
                extra["unreadable_" + extraKey] = value;
                return;
            }

            const auto s = value.get<std::string>();
            bool strange = false;
            for (size_t i = 0; i < s.size() && i < steps.size(); ++i)
            {
                steps[i] = s[i] == 'x' || s[i] == 'X';
                strange |= ! (s[i] == 'x' || s[i] == 'X' || s[i] == '.' || s[i] == '-');
            }
            if (strange)
                warnings.push_back (what + " steps contained characters other than 'x' and '.'; those steps are off.");
            if (s.size() > steps.size())
            {
                warnings.push_back (what + " has " + std::to_string (s.size()) + " steps; BeatForge plays the first 16 and keeps the rest in the file.");
                extra[extraKey + "_after16"] = s.substr (steps.size());
            }
        }

        std::string stepsToString (const std::array<bool, stepsPerPattern>& steps)
        {
            std::string s;
            for (auto on : steps) s += on ? 'x' : '.';
            return s;
        }

        DrumLane readLaneSettings (const json& j, int lane, Reader& r)
        {
            const auto defaultName = std::string (audio::drumSoundName ((audio::DrumSound) lane));
            const auto what = "Drum lane \"" + defaultName + "\"";
            DrumLane l;
            l.name = r.name (j, "name", defaultName, Project::maxLaneNameLength, what + " name", false);
            l.gainDb = r.number (j, "gainDb", 0.0f, audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb, what + " volume");
            l.muted = r.boolean (j, "muted", false, what + " mute");
            l.soloed = r.boolean (j, "soloed", false, what + " solo");
            return l;
        }

        //==============================================================================
        json paramsToJson (const Track& t, audio::InstrumentType type)
        {
            json out = objectOr (t.extraInstrumentParams[(size_t) type]);
            for (int i = 0; i < audio::numParams; ++i)
            {
                const auto param = (audio::Param) i;
                const auto value = t.instrumentParams[(size_t) type][(size_t) i];
                const auto id = std::string (audio::paramSpec (param).id);
                if (param == audio::Param::wave)
                    out[id] = waveNames[std::clamp ((int) value, 0, 3)];
                else
                    out[id] = value;
            }
            return out;
        }

        void readParams (const json& source, Track& t, audio::InstrumentType type, std::vector<std::string>& warnings)
        {
            const auto name = std::string (audio::instrumentName (type));
            std::set<std::string> known;
            int corrected = 0;

            for (int i = 0; i < audio::numParams; ++i)
            {
                const auto param = (audio::Param) i;
                const auto& spec = audio::paramSpec (param);
                const auto id = std::string (spec.id);
                known.insert (id);
                if (! source.contains (id))
                    continue;

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
                t.instrumentParams[(size_t) type][(size_t) i] = value;
            }

            if (corrected > 0)
                warnings.push_back (std::to_string (corrected) + " " + name + " setting(s) were out of range or unreadable and were corrected.");

            json extra = json::object();
            for (auto it = source.begin(); it != source.end(); ++it)
                if (! known.contains (it.key()))
                    extra[it.key()] = it.value();
            t.extraInstrumentParams[(size_t) type] = extra.dump();
        }

        // Params object { "polySynth": {...}, "monoBass": {...}, <unknown>: ... }.
        void readInstrumentParams (const json& track, Track& t, std::vector<std::string>& warnings, json& trackExtra)
        {
            if (! track.contains ("params"))
                return;
            const auto& params = track["params"];
            if (! params.is_object())
            {
                warnings.push_back ("The instrument settings of \"" + t.name + "\" were unreadable; using defaults.");
                return;
            }
            json unknown = json::object();
            for (auto it = params.begin(); it != params.end(); ++it)
            {
                if (it.key() == "polySynth" && it.value().is_object())      readParams (it.value(), t, audio::InstrumentType::polySynth, warnings);
                else if (it.key() == "monoBass" && it.value().is_object())  readParams (it.value(), t, audio::InstrumentType::monoBass, warnings);
                else                                                        unknown[it.key()] = it.value();
            }
            if (! unknown.empty())
                trackExtra["otherInstrumentParams"] = unknown;
        }

        // Notes of an instrument clip; ids are assigned 1..n here (files carry none).
        void readNotes (const json& notesJson, Clip& clip, std::vector<std::string>& warnings)
        {
            json invalid = json::array();
            midi::NoteId nextId = 1;
            int corrected = 0;

            for (auto& n : notesJson)
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

                const auto p = std::clamp (note.pitch, midi::minPitch, midi::maxPitch);
                const auto v = std::clamp (note.velocity, midi::minVelocity, midi::maxVelocity);
                if (p != note.pitch || v != note.velocity) ++corrected;
                note.pitch = p;
                note.velocity = v;

                const auto extra = extrasOf (n, { "pitch", "velocity", "start", "length" });
                if (extra != "{}")
                    clip.noteExtra[note.id] = extra;
                clip.notes.push_back (note);
            }

            if (corrected > 0)
                warnings.push_back (std::to_string (corrected) + " note(s) had a pitch or velocity out of range and were limited.");
            if (! invalid.empty())
                warnings.push_back (std::to_string (invalid.size()) + " note(s) were unreadable; they are kept in the file but not shown.");
            clip.invalidNotes = invalid.dump();
        }

        json notesToJson (const Clip& clip)
        {
            json notes = json::array();
            for (auto& n : clip.notes)
            {
                json note = { { "pitch", n.pitch }, { "velocity", n.velocity }, { "start", n.start }, { "length", n.length } };
                if (auto it = clip.noteExtra.find (n.id); it != clip.noteExtra.end())
                    mergeInto (note, it->second);
                notes.push_back (note);
            }
            for (auto& raw : parseOr (clip.invalidNotes, json::array()))
                notes.push_back (raw);
            return notes;
        }

        //==============================================================================
        bool isInstrumentId (const json& v)
        {
            return v == "basicSynth" || v == "polySynth" || v == "monoBass";
        }

        void sortClips (Track& t)
        {
            std::stable_sort (t.clips.begin(), t.clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        }

        //==============================================================================
        // Version 2: tracks with clips.
        void readTracksV2 (const json& j, Project& p, std::vector<std::string>& warnings)
        {
            Reader r { warnings };
            json extraTracks = json::array();
            TrackId nextTrackId = 1;
            ClipId nextClipId = 1;

            if (! j.contains ("tracks") || ! j["tracks"].is_array())
            {
                warnings.push_back ("The project had no tracks.");
                return;
            }

            for (auto& tj : j["tracks"])
            {
                const bool drums = tj.is_object() && tj.contains ("type") && tj["type"] == "drumMachine";
                const bool instrument = tj.is_object() && tj.contains ("type") && tj["type"] == "instrument"
                                     && tj.contains ("instrument") && isInstrumentId (tj["instrument"]);

                if ((! drums && ! instrument) || (int) p.tracks.size() >= Project::maxTracks)
                {
                    extraTracks.push_back (tj);
                    warnings.push_back ((int) p.tracks.size() >= Project::maxTracks
                                            ? "A track beyond the 16-track limit was kept in the file but is not shown."
                                            : "A track this version cannot play was kept in the file but is not shown.");
                    continue;
                }

                Track t;
                t.id = nextTrackId++;
                t.type = drums ? TrackType::drums : TrackType::instrument;
                t.name = r.name (tj, "name", drums ? "Drums" : "Synth", Project::maxNameLength, "Track name");
                t.volumeDb = r.number (tj, "volumeDb", 0.0f, Project::minVolumeDb, Project::maxVolumeDb, "\"" + t.name + "\" volume");
                t.pan = r.number (tj, "pan", 0.0f, -1.0f, 1.0f, "\"" + t.name + "\" pan");
                t.muted = r.boolean (tj, "muted", false, "\"" + t.name + "\" mute");
                t.soloed = r.boolean (tj, "soloed", false, "\"" + t.name + "\" solo");

                json trackExtra = parseOr (extrasOf (tj, { "type", "instrument", "name", "volumeDb", "pan", "muted", "soloed",
                                                           "lanes", "params", "clips" }), json::object());

                if (drums && tj.contains ("lanes") && tj["lanes"].is_array())
                {
                    std::array<bool, audio::DrumMachine::numLanes> seen {};
                    json extraLanes = json::array();
                    for (auto& lj : tj["lanes"])
                    {
                        const int lane = lj.is_object() && lj.contains ("sound") && lj["sound"].is_string()
                                             ? laneForSoundId (lj["sound"].get<std::string>()) : -1;
                        if (lane < 0 || seen[(size_t) lane])
                        {
                            extraLanes.push_back (lj);
                            warnings.push_back ("A drum lane with an unknown or duplicate sound was kept in the file but is not shown.");
                            continue;
                        }
                        seen[(size_t) lane] = true;
                        t.lanes[(size_t) lane] = readLaneSettings (lj, lane, r);
                        t.lanes[(size_t) lane].extra = extrasOf (lj, { "sound", "name", "gainDb", "muted", "soloed" });
                    }
                    t.extraLanes = extraLanes.dump();
                }

                if (instrument)
                {
                    t.instrument = tj["instrument"] == "monoBass" ? audio::InstrumentType::monoBass : audio::InstrumentType::polySynth;
                    readInstrumentParams (tj, t, warnings, trackExtra);
                }

                if (tj.contains ("clips") && tj["clips"].is_array())
                {
                    for (auto& cj : tj["clips"])
                    {
                        const bool ok = cj.is_object() && cj.contains ("start") && cj.contains ("length")
                                     && cj["start"].is_number_integer() && cj["length"].is_number_integer()
                                     && cj["start"].get<std::int64_t>() >= 0 && cj["length"].get<std::int64_t>() >= 1;
                        if (! ok)
                        {
                            trackExtra["unreadableClips"].push_back (cj);
                            warnings.push_back ("A clip on \"" + t.name + "\" was unreadable; it is kept in the file but not shown.");
                            continue;
                        }

                        Clip c;
                        c.id = nextClipId++;
                        c.start = cj["start"].get<std::int64_t>();
                        c.length = std::min<time::Tick> (cj["length"].get<std::int64_t>(), Project::maxClipLength);
                        if (c.length != cj["length"].get<std::int64_t>())
                            warnings.push_back ("A clip on \"" + t.name + "\" was longer than 64 bars and was shortened.");

                        json clipExtra = parseOr (extrasOf (cj, { "start", "length", "pattern", "notes" }), json::object());

                        if (drums && cj.contains ("pattern"))
                        {
                            const auto& pj = cj["pattern"];
                            if (pj.is_object())
                            {
                                json unknownLanes = json::object();
                                for (auto it = pj.begin(); it != pj.end(); ++it)
                                {
                                    const int lane = laneForSoundId (it.key());
                                    if (lane < 0) { unknownLanes[it.key()] = it.value(); continue; }
                                    readSteps (it.value(), c.pattern[(size_t) lane], clipExtra, it.key(),
                                               "Pattern lane \"" + it.key() + "\"", warnings);
                                }
                                if (! unknownLanes.empty())
                                    clipExtra["otherPatternLanes"] = unknownLanes;
                            }
                            else
                            {
                                clipExtra["unreadablePattern"] = pj;
                                warnings.push_back ("A drum pattern was unreadable; it is kept in the file but not shown.");
                            }
                        }

                        if (instrument && cj.contains ("notes") && cj["notes"].is_array())
                            readNotes (cj["notes"], c, warnings);

                        c.extra = clipExtra.dump();
                        t.clips.push_back (c);
                    }
                }

                sortClips (t);
                t.extra = trackExtra.dump();
                p.tracks.push_back (std::move (t));
            }

            p.extraTracks = extraTracks.dump();
        }

        //==============================================================================
        // Version 1 (0.4-0.6): one drum track with a looping pattern and one
        // synth track with one looping clip. Upgraded so it plays the same: both
        // become clips at bar 1 and the loop covers them.
        void readTracksV1 (const json& j, Project& p, std::vector<std::string>& warnings)
        {
            Reader r { warnings };
            Track drums;
            drums.id = 1;
            drums.type = TrackType::drums;
            drums.name = "Drums";
            Clip beat;
            beat.id = 1;

            Track synth;
            synth.id = 2;
            synth.type = TrackType::instrument;
            synth.name = "Synth";
            std::optional<Clip> synthClip;

            json extraTracks = json::array();
            bool haveDrums = false, haveSynth = false;

            if (j.contains ("tracks") && j["tracks"].is_array())
            {
                for (auto& tj : j["tracks"])
                {
                    const bool isDrums = tj.is_object() && tj.contains ("type") && tj["type"] == "drumMachine";
                    const bool isSynth = tj.is_object() && tj.contains ("type") && tj["type"] == "instrument"
                                      && tj.contains ("instrument") && isInstrumentId (tj["instrument"]);

                    if (isSynth && ! haveSynth)
                    {
                        haveSynth = true;
                        synth.name = r.name (tj, "name", "Synth", Project::maxNameLength, "Synth track name");
                        synth.instrument = tj["instrument"] == "monoBass" ? audio::InstrumentType::monoBass : audio::InstrumentType::polySynth;
                        json trackExtra = parseOr (extrasOf (tj, { "type", "instrument", "name", "clip", "params" }), json::object());
                        readInstrumentParams (tj, synth, warnings, trackExtra);

                        if (tj.contains ("clip") && tj["clip"].is_object())
                        {
                            const auto& cj = tj["clip"];
                            Clip c;
                            c.id = 2;
                            if (cj.contains ("lengthTicks") && cj["lengthTicks"].is_number_integer()
                                && cj["lengthTicks"].get<std::int64_t>() > 0 && cj["lengthTicks"].get<std::int64_t>() <= 32 * 3840)
                                c.length = cj["lengthTicks"].get<std::int64_t>();
                            else
                            {
                                c.length = 4 * patternTicks;
                                warnings.push_back ("The synth clip length was missing or invalid; using 4 bars.");
                            }
                            if (cj.contains ("notes") && cj["notes"].is_array())
                                readNotes (cj["notes"], c, warnings);
                            c.extra = extrasOf (cj, { "lengthTicks", "notes" });
                            synthClip = c;
                        }
                        else if (tj.contains ("clip"))
                        {
                            warnings.push_back ("The synth clip was unreadable; it is kept in the file but not shown.");
                            trackExtra["unreadableClip"] = tj["clip"];
                        }
                        synth.extra = trackExtra.dump();
                        continue;
                    }

                    if (haveDrums || ! isDrums)
                    {
                        extraTracks.push_back (tj);
                        warnings.push_back ("A track this version cannot play was kept in the file but is not shown.");
                        continue;
                    }

                    haveDrums = true;
                    drums.name = r.name (tj, "name", "Drums", Project::maxNameLength, "Drum track name");
                    json clipExtra = json::object();
                    std::array<bool, audio::DrumMachine::numLanes> seen {};
                    json extraLanes = json::array();

                    if (tj.contains ("lanes") && tj["lanes"].is_array())
                    {
                        for (auto& lj : tj["lanes"])
                        {
                            const int lane = lj.is_object() && lj.contains ("sound") && lj["sound"].is_string()
                                                 ? laneForSoundId (lj["sound"].get<std::string>()) : -1;
                            if (lane < 0 || seen[(size_t) lane])
                            {
                                extraLanes.push_back (lj);
                                warnings.push_back (lane < 0 ? "A drum lane with an unknown sound was kept in the file but cannot play."
                                                             : "A duplicate drum lane was kept in the file but is not shown.");
                                continue;
                            }
                            seen[(size_t) lane] = true;
                            drums.lanes[(size_t) lane] = readLaneSettings (lj, lane, r);
                            drums.lanes[(size_t) lane].extra = extrasOf (lj, { "sound", "name", "gainDb", "muted", "soloed", "steps" });
                            if (lj.contains ("steps"))
                                readSteps (lj["steps"], beat.pattern[(size_t) lane], clipExtra, soundId (lane),
                                           "Drum lane \"" + drums.lanes[(size_t) lane].name + "\"", warnings);
                        }
                    }
                    else
                    {
                        warnings.push_back ("The drum track had no lanes; starting with an empty pattern.");
                    }

                    drums.extraLanes = extraLanes.dump();
                    drums.extra = extrasOf (tj, { "type", "name", "lanes" });
                    beat.extra = clipExtra.dump();
                }
            }

            if (! haveDrums)
                warnings.push_back ("The project had no drum machine track; starting with an empty pattern.");

            // Same sound as in 0.6: everything loops from bar 1 over the longest part.
            const auto songLength = std::max (patternTicks, synthClip ? synthClip->length : 0);
            beat.length = ((songLength + patternTicks - 1) / patternTicks) * patternTicks;
            drums.clips.push_back (beat);
            if (synthClip)
                synth.clips.push_back (*synthClip);

            int loopBars = 4;
            bool loopEnabled = false;
            if (j.contains ("loop") && j["loop"].is_object())
            {
                loopEnabled = r.boolean (j["loop"], "enabled", false, "Loop setting");
                if (j["loop"].contains ("bars") && j["loop"]["bars"].is_number_integer())
                    loopBars = std::clamp (j["loop"]["bars"].get<int>(), 1, 64);
            }
            p.loopEnabled = true;
            p.loopStart = 0;
            p.loopEnd = loopEnabled ? std::min<time::Tick> (loopBars * p.timeSignature.ticksPerBar(), beat.length) : beat.length;
            if (p.loopEnd <= p.loopStart)
                p.loopEnd = beat.length;

            p.tracks = { drums, synth };
            p.extraTracks = extraTracks.dump();
        }

        //==============================================================================
        json trackToJson (const Track& t)
        {
            json tj = objectOr (t.extra);
            json otherParams = tj.contains ("otherInstrumentParams") ? tj["otherInstrumentParams"] : json::object();
            json unreadableClips = tj.contains ("unreadableClips") ? tj["unreadableClips"] : json::array();
            tj.erase ("otherInstrumentParams");
            tj.erase ("unreadableClips");

            tj["type"] = t.type == TrackType::drums ? "drumMachine" : "instrument";
            if (t.type == TrackType::instrument)
                tj["instrument"] = std::string (audio::instrumentId (t.instrument));
            tj["name"] = t.name;
            tj["volumeDb"] = t.volumeDb;
            tj["pan"] = t.pan;
            tj["muted"] = t.muted;
            tj["soloed"] = t.soloed;

            if (t.type == TrackType::drums)
            {
                json lanes = json::array();
                for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
                {
                    const auto& l = t.lanes[(size_t) i];
                    json lj = { { "sound", soundId (i) }, { "name", l.name }, { "gainDb", l.gainDb }, { "muted", l.muted }, { "soloed", l.soloed } };
                    mergeInto (lj, l.extra);
                    lanes.push_back (lj);
                }
                for (auto& raw : parseOr (t.extraLanes, json::array()))
                    lanes.push_back (raw);
                tj["lanes"] = lanes;
            }
            else
            {
                json params = { { "polySynth", paramsToJson (t, audio::InstrumentType::polySynth) },
                                { "monoBass", paramsToJson (t, audio::InstrumentType::monoBass) } };
                for (auto it = otherParams.begin(); it != otherParams.end(); ++it)
                    params[it.key()] = it.value();
                tj["params"] = params;
            }

            json clips = json::array();
            for (auto& c : t.clips)
            {
                json cj = objectOr (c.extra);
                json otherLanes = cj.contains ("otherPatternLanes") ? cj["otherPatternLanes"] : json::object();
                cj.erase ("otherPatternLanes");
                cj["start"] = c.start;
                cj["length"] = c.length;
                if (t.type == TrackType::drums)
                {
                    json pattern = json::object();
                    for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
                        pattern[soundId (i)] = stepsToString (c.pattern[(size_t) i]);
                    for (auto it = otherLanes.begin(); it != otherLanes.end(); ++it)
                        pattern[it.key()] = it.value();
                    cj["pattern"] = pattern;
                }
                else
                {
                    cj["notes"] = notesToJson (c);
                }
                clips.push_back (cj);
            }
            for (auto& raw : unreadableClips)
                clips.push_back (raw);
            tj["clips"] = clips;
            return tj;
        }
    }

    //==============================================================================
    std::string toJson (const Project& p, const std::string& appVersion)
    {
        json j = objectOr (p.extraTopLevel);
        j["format"] = formatName;
        j["version"] = currentVersion;
        j["savedBy"] = "BeatForge " + appVersion;
        j["name"] = p.name;
        j["tempo"] = p.tempo;
        j["timeSignature"] = { { "numerator", p.timeSignature.numerator }, { "denominator", p.timeSignature.denominator } };
        j["metronome"] = p.metronomeEnabled;
        j["loop"] = { { "enabled", p.loopEnabled }, { "start", p.loopStart }, { "end", p.loopEnd } };

        json tracks = json::array();
        for (auto& t : p.tracks)
            tracks.push_back (trackToJson (t));
        for (auto& raw : parseOr (p.extraTracks, json::array()))
            tracks.push_back (raw);
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
        result.fileVersion = version;
        if (version > currentVersion)
        {
            result.error = "This project was saved by a newer BeatForge (file format version " + std::to_string (version)
                         + "). This version reads up to version " + std::to_string (currentVersion)
                         + ". The file was not changed.";
            return result;
        }

        auto& warnings = result.warnings;
        Reader r { warnings };
        Project p;

        p.name = r.name (j, "name", "Untitled", Project::maxNameLength, "Project name");

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
            p.timeSignature = { sig["numerator"].get<int>(), sig["denominator"].get<int>() };
        else
            warnings.push_back ("Time signature was missing or invalid; using 4/4.");

        p.metronomeEnabled = r.boolean (j, "metronome", false, "Metronome setting");

        if (version == 1)
        {
            readTracksV1 (j, p, warnings);
        }
        else
        {
            if (j.contains ("loop") && j["loop"].is_object())
            {
                const auto& loop = j["loop"];
                p.loopEnabled = r.boolean (loop, "enabled", false, "Loop setting");
                const bool startOk = loop.contains ("start") && loop["start"].is_number_integer() && loop["start"].get<std::int64_t>() >= 0;
                const bool endOk = loop.contains ("end") && loop["end"].is_number_integer();
                if (startOk && endOk && loop["end"].get<std::int64_t>() > loop["start"].get<std::int64_t>())
                {
                    p.loopStart = loop["start"].get<std::int64_t>();
                    p.loopEnd = loop["end"].get<std::int64_t>();
                }
                else if (loop.contains ("start") || loop.contains ("end"))
                {
                    warnings.push_back ("The loop region was invalid; using bars 1-4.");
                }
            }
            readTracksV2 (j, p, warnings);
        }

        p.extraTopLevel = extrasOf (j, { "format", "version", "savedBy", "name", "tempo", "timeSignature",
                                         "metronome", "loop", "tracks" });
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
