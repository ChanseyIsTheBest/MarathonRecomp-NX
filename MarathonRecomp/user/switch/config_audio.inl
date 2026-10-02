// [Switch] keys of the audio area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);

// Game audio through audout, fed from the audio pump thread, instead of SDL's audio thread (which runs at the
// guest threads' time-sliced priority and can stop playing for good once starved). Same samples, bit for bit.
// A surround channel configuration keeps the SDL path.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAudioOut, true, true);

// Audio pump and XMA decoder threads pinned to SwitchAudioThreadCore. Without it they may run on any of the
// application's cores like every other thread (the guest's sync/lwsync are real fences in this build). Off until an
// on-console audio test.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAudioThreadCores, true, true);
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchAudioThreadCore, 2, true);

// XMA decoder threads sleep until they can make progress (woken by the guest's resume/submit/flush) instead of
// waking every 2 ms. Off until an on-console music/cutscene audio test.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchXmaEventWait, true, true);

// Vibration values equal to the ones last sent to the same controllers are not sent again (one hid IPC per
// XamInputSetState otherwise); they are still re-sent every 250 ms and after any controller change.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVibrationDedupe, true, true);
