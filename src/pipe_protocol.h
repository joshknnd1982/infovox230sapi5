#pragma once

#include <stdint.h>

// The engine dlls are all 32-bit, so a 64-bit SAPI host (Windows 11 Narrator, 64-bit
// Balabolka, 64-bit NVDA) cannot load them in process. The 64-bit build of the SAPI
// engine therefore talks to a 32-bit worker over this pipe. All text preparation --
// sanitizing, transliteration, number words, inline commands, encoding -- happens on
// the SAPI side, which is architecture independent, so the worker only ever receives
// finished bytes and the handful of numbers that drive the audio path.
#define BESTSPEECH_PIPE_NAME L"\\\\.\\pipe\\BestspeechTTS"

// Bumped whenever the wire format changes. A worker from a previous install can still
// be running when a new version starts speaking -- it lives as long as any process
// keeps it busy, and the pipe name does not change between versions -- so the client
// checks this on connect and replaces a worker that does not match. Without that an
// upgrade silently keeps talking to the old worker, which reads the SpeakCommand fields
// at the offsets it was built for and quietly ignores rate and volume.
#define BESTSPEECH_PROTOCOL_VERSION 2u

enum PipeCommand : uint32_t {
    CMD_PING = 0,
    CMD_SPEAK = 2,
    CMD_STOP = 3,
    CMD_SHUTDOWN = 4,
    CMD_GET_SAMPLE_RATE = 6,
};

enum PipeResponse : uint32_t {
    RESP_OK = 0,
    RESP_ERROR = 1,
    RESP_AUDIO_DATA = 2,
    RESP_AUDIO_END = 3,
    RESP_SAMPLE_RATE = 4,
    RESP_PONG = 5,
};

#pragma pack(push, 1)
struct PipeMessageHeader {
    uint32_t type;
    uint32_t size;
};

struct SpeakCommand {
    int32_t  engine_index;   // index into Bestspeech::engines[]
    float    sonic_speed;    // 1.0 bypasses the time stretcher
    float    gain_scale;     // 1.0 bypasses software volume
    uint32_t text_length;    // bytes of prepared text following this struct
};

struct SampleRateQuery {
    int32_t engine_index;
};

struct AudioDataChunk {
    uint32_t size;
};
#pragma pack(pop)

#define BESTSPEECH_BITS_PER_SAMPLE 16
#define BESTSPEECH_CHANNELS 1
