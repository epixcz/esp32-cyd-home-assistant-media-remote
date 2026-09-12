#pragma once
#include <stdint.h>
namespace media_remote {
// Callbacks supply the single service attempt. Rejected commands leave every
// confirmed field unchanged; no automatic retry is performed here.
template<class State, class Send>
bool confirmSeek(State &state, unsigned long &anchor, long position, uint32_t now, Send send) {
  if (state.durationMs <= 0) return false;
  if (position < 0) position = 0;
  if (position > state.durationMs) position = state.durationMs;
  if (!send(position)) return false;
  state.progressMs = position;
  if (state.playing) anchor = now - position;
  return true;
}
template<class State, class Send>
bool confirmVolume(State &state, int requested, Send send) {
  if (requested < 0) requested = 0;
  if (requested > 100) requested = 100;
  if (requested == state.volume) return false;
  if (!send(requested)) return false;
  state.volume = requested;
  return true;
}
template<class State, class Send>
bool confirmPlayPause(State &state, unsigned long &anchor, uint32_t now, Send send) {
  if (!send()) return false;
  if (state.playing && anchor) {
    long position = static_cast<uint32_t>(now - anchor);
    state.progressMs = position > state.durationMs ? state.durationMs : position;
  } else if (!state.playing) {
    anchor = now - state.progressMs;
  }
  state.playing = !state.playing;
  return true;
}
} // namespace media_remote
