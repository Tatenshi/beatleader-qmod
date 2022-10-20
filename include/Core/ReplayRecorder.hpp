#pragma once

#include "include/Models/Replay.hpp"

namespace ReplayRecorder {
    void StartRecording(function<void(Replay const &, bool)> const &callback);
    static bool recording;
}