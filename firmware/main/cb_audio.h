// cb_audio.h - tone output. Port step 5 (Softwarearchitektur 5.5).
//
// The task "audio" is the only owner of I2S1 (amplifier) and AMP_SD_MODE (Softwarearchitektur 3.2). Others
// only queue requests with cb_audio_play(); the call never blocks. The task:
//   open the WAV on LittleFS -> parse (cb_wav) -> adapt the I2S sample rate if needed -> SD_MODE high ->
//   20 ms silence (amplifier start-up) -> stream 512-byte blocks, scaled by the volume -> 30 ms silence ->
//   SD_MODE low (amplifier off between tones: no hiss, less current).
// Before each block it looks for a newer request; a higher or equal priority (cb_audio_logic.h) replaces
// the running tone. i2s.write() blocks until DMA space is free (bring-up 3.4.1) - that paces the task.
//
// Missing or broken files, or a file system that does not mount: the unit stays silent and logs it.
// Audio is never a reason for a crash - the door must still open without sound.
//
// Tone files (16-bit WAV) come from tools/toene/erzeuge_toene.py and are flashed as a LittleFS image with
//   python tools/cb.py toene COM4
// Since step 8 they can also be uploaded over the status page in maintenance mode (cb_audio_install).
//
// Console: play <ring|ok|error|nonet|tick|<Hz>>, vol [next|15|30|50|75|100], files.
#pragma once

#include <cstdint>

#include "cb_audio_logic.h"
#include "cb_json.h"

void cb_audio_begin();                 // boot steps 8 + 9: mount LittleFS, configure I2S1, start the task
void cb_audio_play(cb::Sound s);       // queue a tone, never blocks
void cb_audio_tone(uint16_t hz, uint16_t ms);  // queue a pure sine (diagnosis / acoustic self-check)
void cb_audio_volume_step();           // boot button short press: next step, store, play a tick
uint8_t cb_audio_volume();             // current user volume (15..100 %)
bool cb_audio_busy();                  // a tone is playing right now
bool cb_audio_fs_ok();                 // LittleFS mounted
// "ring" | "ok" | "error" | "nonet" | "tick" -> Sound (console and upload URL use the same names).
cb::Sound cb_audio_parse_sound(const char *name, bool *known);
// Status page: "audio": {...} with volume, last file, the tone files (cached, no flash access while playing).
void cb_audio_json(cb::Json &j);
// Factory reset: copies /werk/<file> (put there by the factory tone image) over each tone file. Any task.
void cb_audio_restore_factory_tones();
// Tone upload (step 8, SA 6.3): checks `tmp_path` as 16-bit PCM WAV and renames it onto the file of `target`
// while no tone plays (waits for a running tone up to 6 s). false = refused, `msg` says why; the caller
// removes the temp file. true: `msg` describes the new file. Callable from any task.
bool cb_audio_install(const char *tmp_path, cb::Sound target, char *msg, size_t msg_len);
