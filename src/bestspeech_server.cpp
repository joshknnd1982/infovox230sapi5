// 32-bit worker process for the 64-bit SAPI engine.
//
// Holds one loaded instance of each language dll that has been asked for, so switching
// between voices of the same language costs nothing and switching language costs one
// load. Text arrives fully prepared; this process only synthesizes and streams pcm back.

#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>

#include "pipe_protocol.h"
#include "b32_wrapper.h"
#include "engines.hpp"
#include "debug_log.h"

namespace {

constexpr wchar_t BESTSPEECH_SERVER_MUTEX[] = L"Global\\BestspeechTTSServerMutex";

b32::StatePtr g_engines[Bestspeech::engine_count];
HANDLE g_pipe = INVALID_HANDLE_VALUE;
HANDLE g_mutex = nullptr;
bool g_stop_speaking = false;
bool g_shutdown_requested = false;
std::wstring g_install_dir;

bool write_all(HANDLE pipe, const void* data, DWORD size) {
    auto* p = static_cast<const char*>(data);
    DWORD remaining = size;
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, p, remaining, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        remaining -= written;
    }
    return true;
}

bool read_all(HANDLE pipe, void* data, DWORD size) {
    auto* p = static_cast<char*>(data);
    DWORD remaining = size;
    while (remaining > 0) {
        DWORD got = 0;
        if (!ReadFile(pipe, p, remaining, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        remaining -= got;
    }
    return true;
}

void send_response(HANDLE pipe, PipeResponse resp, const void* data = nullptr, uint32_t size = 0) {
    PipeMessageHeader header{ static_cast<uint32_t>(resp), size };
    if (!write_all(pipe, &header, sizeof(header))) {
        return;
    }
    if (data && size > 0) {
        write_all(pipe, data, size);
    }
}

bool audio_callback(const char* data, long size, void* user) {
    auto pipe = static_cast<HANDLE>(user);

    if (g_stop_speaking) {
        return false;
    }

    PipeMessageHeader header{ RESP_AUDIO_DATA,
                              static_cast<uint32_t>(sizeof(uint32_t) + size) };
    const auto chunk_size = static_cast<uint32_t>(size);

    return write_all(pipe, &header, sizeof(header)) &&
           write_all(pipe, &chunk_size, sizeof(chunk_size)) &&
           write_all(pipe, data, static_cast<DWORD>(size));
}

// Loads a language dll on first use and keeps it for the life of the process.
b32::State* engine_for(int index) {
    if (index < 0 || index >= Bestspeech::engine_count) {
        return nullptr;
    }
    if (!g_engines[index]) {
        const std::wstring path = g_install_dir + Bestspeech::engines[index].dll;
        g_engines[index] = b32::init(path.c_str());
        if (g_engines[index]) {
            DEBUG_LOG("worker: loaded engine %s from %ls (sample rate %lu)",
                      Bestspeech::engines[index].id, path.c_str(),
                      b32::get_sample_rate(g_engines[index].get()));
        } else {
            const bool present = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
            DEBUG_LOG("worker: FAILED to open engine %s: %ls (file %s, last error %lu)",
                      Bestspeech::engines[index].id, path.c_str(),
                      present ? "exists" : "IS MISSING", GetLastError());
        }
    }
    return g_engines[index].get();
}

void handle_get_sample_rate(HANDLE pipe, const char* payload, uint32_t size) {
    if (size < sizeof(SampleRateQuery)) {
        send_response(pipe, RESP_ERROR);
        return;
    }
    const auto* q = reinterpret_cast<const SampleRateQuery*>(payload);
    b32::State* s = engine_for(q->engine_index);
    uint32_t rate = 0;
    if (s) {
        rate = b32::get_sample_rate(s);
    }
    if (rate == 0 && q->engine_index >= 0 && q->engine_index < Bestspeech::engine_count) {
        rate = Bestspeech::engines[q->engine_index].sample_rate;
    }
    send_response(pipe, RESP_SAMPLE_RATE, &rate, sizeof(rate));
}

void handle_speak(HANDLE pipe, const char* payload, uint32_t payload_size) {
    if (payload_size < sizeof(SpeakCommand)) {
        send_response(pipe, RESP_ERROR);
        return;
    }

    const auto* cmd = reinterpret_cast<const SpeakCommand*>(payload);
    if (payload_size < sizeof(SpeakCommand) + cmd->text_length) {
        send_response(pipe, RESP_ERROR);
        return;
    }

    b32::State* state = engine_for(cmd->engine_index);
    if (!state) {
        DEBUG_LOG("worker: no engine for index %d, utterance dropped", cmd->engine_index);
        send_response(pipe, RESP_ERROR);
        return;
    }

    const std::string text(payload + sizeof(SpeakCommand), cmd->text_length);

    g_stop_speaking = false;

    b32::SpeakParams params;
    params.text = text.c_str();
    params.sonic_speed = cmd->sonic_speed;
    params.gain_scale = cmd->gain_scale;

    DEBUG_LOG("worker: speak engine=%s speed=%.2fx gain=%.2fx bytes=%u",
              Bestspeech::engines[cmd->engine_index].id,
              cmd->sonic_speed, cmd->gain_scale, cmd->text_length);

    b32::speak_async(state, audio_callback, pipe, params);

    send_response(pipe, RESP_AUDIO_END);
}

void handle_client(HANDLE pipe) {
    while (true) {
        PipeMessageHeader header;
        if (!read_all(pipe, &header, sizeof(header))) {
            break;
        }

        std::vector<char> payload;
        if (header.size > 0) {
            // A malformed or hostile length must not be turned into a huge allocation.
            if (header.size > (16u << 20)) {
                break;
            }
            payload.resize(header.size);
            if (!read_all(pipe, payload.data(), header.size)) {
                break;
            }
        }

        switch (static_cast<PipeCommand>(header.type)) {
            case CMD_PING: {
                const uint32_t version = BESTSPEECH_PROTOCOL_VERSION;
                send_response(pipe, RESP_PONG, &version, sizeof(version));
                break;
            }
            case CMD_GET_SAMPLE_RATE:
                handle_get_sample_rate(pipe, payload.data(), header.size);
                break;
            case CMD_SPEAK:
                handle_speak(pipe, payload.data(), header.size);
                break;
            case CMD_STOP:
                g_stop_speaking = true;
                send_response(pipe, RESP_OK);
                break;
            case CMD_SHUTDOWN:
                send_response(pipe, RESP_OK);
                g_shutdown_requested = true;
                return;
            default:
                send_response(pipe, RESP_ERROR);
                break;
        }
    }
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    g_mutex = CreateMutexW(nullptr, TRUE, BESTSPEECH_SERVER_MUTEX);
    if (!g_mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        return 0;  // another worker already serves this session
    }

    // Engine dlls sit next to this executable.
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (wchar_t* last_slash = wcsrchr(exe_path, L'\\')) {
        *(last_slash + 1) = L'\0';
    }
    g_install_dir = exe_path;

    DEBUG_LOG("worker: starting, install dir %ls", g_install_dir.c_str());

    if (!b32::load_shim(g_install_dir.c_str())) {
        DEBUG_LOG("worker: could not load b32_wrapper.dll, exiting");
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        return 1;
    }

    while (!g_shutdown_requested) {
        g_pipe = CreateNamedPipeW(
            BESTSPEECH_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0, nullptr);

        if (g_pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        if (ConnectNamedPipe(g_pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            handle_client(g_pipe);
        }

        DisconnectNamedPipe(g_pipe);
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }

    ReleaseMutex(g_mutex);
    CloseHandle(g_mutex);
    return 0;
}
