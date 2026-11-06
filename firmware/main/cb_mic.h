// cb_mic.h - microphone, for DIAGNOSIS ONLY: does the loudspeaker really sound?
//
// Privacy by default (Softwarearchitektur 5.7, 7.8): in normal operation the microphone does not run -
// no clock, no data in any buffer. It is switched on only by these console commands, which need the USB
// cable at the unit (physical access), for at most a few seconds, and nothing is stored or sent: only
// levels (dBFS numbers) go to the log. Every switch-on and -off is logged.
//
// Console:
//   audio-check <ring|ok|error|nonet|tick|<Hz>>
//       measures 0.4 s of silence, plays the tone, measures while it plays, reports both levels and the
//       difference. With a frequency, additionally the level of exactly that frequency (Goertzel).
//       Verdict "HEARD" from +10 dB above the silence.
//   listen <seconds> [Hz]
//       records up to 10 s and logs the level every 250 ms - e.g. to hear the OTHER board ring.
//
// Hardware: IM72D128V01 (PDM) on I2S0 (the only controller with PDM input), 24 kHz, decimation x64 -
// the configuration that passed bring-up stage 4.
#pragma once

void cb_mic_begin();  // registers the console commands; the microphone stays OFF
