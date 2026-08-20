#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "pipe_protocol.h"

class CriticalLock {
public:
    explicit CriticalLock(CRITICAL_SECTION* cs) noexcept : cs_(cs) {
        EnterCriticalSection(cs_);
    }

    ~CriticalLock() {
        LeaveCriticalSection(cs_);
    }

    CriticalLock(const CriticalLock&) = delete;
    CriticalLock& operator=(const CriticalLock&) = delete;

private:
    CRITICAL_SECTION* cs_;
};

using PipeAudioCallback = bool(*)(const char* data, uint32_t size, void* user);

class PipeClient {
public:
    PipeClient();
    ~PipeClient();

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    bool connect();

    // The engine's true output rate, as reported by the worker. Zero if unavailable,
    // in which case the caller falls back to the rate recorded in engines.hpp.
    uint32_t getSampleRate(int engineIndex);

    bool speak(const char* text, uint32_t textLength, int engineIndex,
               float sonicSpeed, float gainScale,
               PipeAudioCallback callback, void* user);

    void shutdownServer();

private:
    bool ensureConnected();
    bool handshakeOk();
    bool replaceStaleServer();
    void disconnect();
    bool isServerRunning();
    bool launchServer();
    bool sendCommand(PipeCommand cmd, const void* data = nullptr, uint32_t size = 0);
    bool readResponse(PipeResponse& resp, std::vector<char>& data);
    bool writeAll(const void* data, uint32_t size);
    bool readAll(void* data, uint32_t size);

    HANDLE pipe_;
    HANDLE serverProcess_;
    std::wstring serverPath_;
    CRITICAL_SECTION cs_;
};
