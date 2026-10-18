#pragma once

#include "beatforge/audio/TrackSchedule.h"
#include "beatforge/project/Project.h"

#include <memory>

namespace beatforge::project
{
    // Voices are keyed by clip and note so notes in different clips never mix up.
    inline audio::VoiceId voiceId (ClipId clip, midi::NoteId note) noexcept { return (audio::VoiceId) clip << 32 | note; }

    // A clip plays until its end or until the next clip on the track starts,
    // whichever is first. Drum patterns repeat every bar; notes beyond a clip's
    // length are silent; notes are cut at the clip's end.
    std::unique_ptr<audio::TrackSchedule> buildSchedule (const Track&);
}
