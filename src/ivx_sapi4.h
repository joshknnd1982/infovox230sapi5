#pragma once

// The slice of SAPI4 that the Infovox 230 engine implements.
//
// These are NOT taken from a SAPI4 SDK header -- nothing from the SAPI4 runtime
// is used, installed or required. They are the vtable layouts the engine itself
// exposes, cross-checked three ways:
//   * the IIDs are the ones embedded in Ivx230nt.dll,
//   * the layouts match NVDA's long-standing sapi4 synth driver, which has
//     driven engines of this generation for years,
//   * the whole set is exercised by tools/ivx_probe.cpp against the real dll.
//
// Everything here is wide-character (the "W" interfaces). The engine's own
// registry and file handling is ANSI, but its COM surface is not.

#include <windows.h>
#include <unknwn.h>

// WAVEFORMATEX, which the engine hands us through IAudio::WaveFormatSet.
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the multimedia headers,
// so ask for the one piece we need directly.
#include <mmreg.h>

namespace ivx {
namespace sapi4 {

typedef unsigned __int64 QWORD;

constexpr int kSvfnLen = 262;
constexpr int kLangLen = 64;
constexpr int kNameLen = kSvfnLen;
constexpr int kStyleLen = kSvfnLen;

// TextData() flags.
constexpr DWORD TTSDATAFLAG_TAGGED = 1;

// TTSMODEINFO::dwFeatures bits. The Infovox modes report 0x19F, i.e. volume,
// speed, pitch and fixed audio among others.
constexpr DWORD TTSFEATURE_ANYWORD = 1;
constexpr DWORD TTSFEATURE_VOLUME = 2;
constexpr DWORD TTSFEATURE_SPEED = 4;
constexpr DWORD TTSFEATURE_PITCH = 8;
constexpr DWORD TTSFEATURE_TAGGED = 16;
constexpr DWORD TTSFEATURE_IPAUNICODE = 32;
constexpr DWORD TTSFEATURE_VISUAL = 64;
constexpr DWORD TTSFEATURE_PCOPTIMIZED = 128;
constexpr DWORD TTSFEATURE_PHONEEVENTS = 256;
constexpr DWORD TTSFEATURE_WORDEVENTS = 512;
constexpr DWORD TTSFEATURE_FIXEDAUDIO = 1024;
constexpr DWORD TTSFEATURE_SAPI4 = 2048;

// Sentinels: setting one of these and reading the value back is how the engine
// is asked what its real range is. There is no other way to discover it.
constexpr WORD TTSATTR_MINPITCH = 0;
constexpr WORD TTSATTR_MAXPITCH = 0xFFFF;
constexpr DWORD TTSATTR_MINSPEED = 0;
constexpr DWORD TTSATTR_MAXSPEED = 0xFFFFFFFF;
constexpr DWORD TTSATTR_MINVOLUME = 0;
constexpr DWORD TTSATTR_MAXVOLUME = 0xFFFFFFFF;

enum VOICECHARSET {
    CHARSET_TEXT = 0,
    CHARSET_IPAPHONETIC = 1,
    CHARSET_ENGINEPHONETIC = 2,
};

#pragma pack(push, 8)

struct LANGUAGEW {
    LANGID LanguageID;
    WCHAR szDialect[kLangLen];
};

struct TTSMODEINFOW {
    GUID gEngine;
    WCHAR szMfgName[kNameLen];
    WCHAR szProductName[kNameLen];
    GUID gModeID;
    WCHAR szModeName[kNameLen];
    LANGUAGEW language;
    WCHAR szSpeaker[kNameLen];
    WCHAR szStyle[kStyleLen];
    WORD wGender;
    WORD wAge;
    DWORD dwFeatures;
    DWORD dwInterfaces;
    DWORD dwEngineFeatures;
};

struct SDATA {
    void* pData;
    DWORD dwSize;
};

struct TTSMOUTH {
    BYTE bMouthHeight;
    BYTE bMouthWidth;
    BYTE bMouthUpturn;
    BYTE bJawOpen;
    BYTE bTeethUpperVisible;
    BYTE bTeethLowerVisible;
    BYTE bTonguePosn;
    BYTE bLipTension;
};

#pragma pack(pop)

// The engine writes into these structures across the COM boundary, so a layout
// that drifts would corrupt the stack rather than fail cleanly.
static_assert(sizeof(LANGUAGEW) == 130, "LANGUAGEW layout changed");
static_assert(sizeof(TTSMODEINFOW) == 2800, "TTSMODEINFOW layout changed");
static_assert(sizeof(SDATA) == sizeof(void*) + sizeof(DWORD) + (sizeof(void*) == 8 ? 4 : 0),
              "SDATA layout changed");

struct __declspec(uuid("{1287A280-4A47-101B-931A-00AA0047BA4F}")) ITTSAttributesW : public IUnknown {
    STDMETHOD(PitchGet)(WORD* pwPitch) PURE;
    STDMETHOD(PitchSet)(WORD wPitch) PURE;
    STDMETHOD(RealTimeGet)(DWORD* pdwRealTime) PURE;
    STDMETHOD(RealTimeSet)(DWORD dwRealTime) PURE;
    STDMETHOD(SpeedGet)(DWORD* pdwSpeed) PURE;
    STDMETHOD(SpeedSet)(DWORD dwSpeed) PURE;
    STDMETHOD(VolumeGet)(DWORD* pdwVolume) PURE;
    STDMETHOD(VolumeSet)(DWORD dwVolume) PURE;
};

// Implemented on our side. TextDataDone is how an utterance ends; BookMark and
// WordPosition are how the engine reports progress through the text.
struct __declspec(uuid("{E4963D40-C743-11CD-80E5-00AA003E4B50}")) ITTSBufNotifySink
    : public IUnknown {
    STDMETHOD(TextDataDone)(QWORD qTimeStamp, DWORD dwFlags) PURE;
    STDMETHOD(TextDataStarted)(QWORD qTimeStamp) PURE;
    STDMETHOD(BookMark)(QWORD qTimeStamp, DWORD dwMarkNum) PURE;
    STDMETHOD(WordPosition)(QWORD qTimeStamp, DWORD dwByteOffset) PURE;
};

struct __declspec(uuid("{28016060-4A47-101B-931A-00AA0047BA4F}")) ITTSCentralW : public IUnknown {
    STDMETHOD(Inject)(LPCWSTR pszText) PURE;
    STDMETHOD(ModeGet)(TTSMODEINFOW* pModeInfo) PURE;
    STDMETHOD(Phoneme)(VOICECHARSET charset, DWORD dwFlags, SDATA source, SDATA* pDest) PURE;
    STDMETHOD(PosnGet)(QWORD* pqTimeStamp) PURE;
    STDMETHOD(TextData)(VOICECHARSET charset, DWORD dwFlags, SDATA data, void* pNotifySink,
                        GUID iidNotifySink) PURE;
    STDMETHOD(ToFileTime)(QWORD* pqTimeStamp, FILETIME* pFileTime) PURE;
    STDMETHOD(AudioPause)() PURE;
    STDMETHOD(AudioResume)() PURE;
    STDMETHOD(AudioReset)() PURE;
    STDMETHOD(Register)(void* pNotifyInterface, GUID iidNotifyInterface, DWORD* pdwKey) PURE;
    STDMETHOD(UnRegister)(DWORD dwKey) PURE;
};

// Implemented on our side. Visual() is the engine's phoneme/viseme stream.
struct __declspec(uuid("{C0FA8F40-4A46-101B-931A-00AA0047BA4F}")) ITTSNotifySinkW
    : public IUnknown {
    STDMETHOD(AttribChanged)(DWORD dwAttribute) PURE;
    STDMETHOD(AudioStart)(QWORD qTimeStamp) PURE;
    STDMETHOD(AudioStop)(QWORD qTimeStamp) PURE;
    STDMETHOD(Visual)(QWORD qTimeStamp, WCHAR cIPAPhoneme, WCHAR cEnginePhoneme, DWORD dwHints,
                      TTSMOUTH* pTTSMouth) PURE;
};

struct ITTSEnumW;

struct __declspec(uuid("{6B837B20-4A47-101B-931A-00AA0047BA4F}")) ITTSEnumW : public IUnknown {
    STDMETHOD(Next)(ULONG cElements, TTSMODEINFOW* pModeInfo, ULONG* pcFetched) PURE;
    STDMETHOD(Skip)(ULONG cElements) PURE;
    STDMETHOD(Reset)() PURE;
    STDMETHOD(Clone)(ITTSEnumW** ppEnum) PURE;
    STDMETHOD(Select)(GUID gModeID, ITTSCentralW** ppCentral, IUnknown* pAudioDest) PURE;
};

// Implemented on our side: this is the "sound card" the engine talks to.
struct __declspec(uuid("{F546B340-C743-11CD-80E5-00AA003E4B50}")) IAudio : public IUnknown {
    STDMETHOD(Flush)() PURE;
    STDMETHOD(LevelGet)(DWORD* pdwLevel) PURE;
    STDMETHOD(LevelSet)(DWORD dwLevel) PURE;
    STDMETHOD(PassNotify)(void* pNotifyInterface, GUID iidNotifyInterface) PURE;
    STDMETHOD(PosnGet)(QWORD* pqTimeStamp) PURE;
    STDMETHOD(Claim)() PURE;
    STDMETHOD(UnClaim)() PURE;
    STDMETHOD(Start)() PURE;
    STDMETHOD(Stop)() PURE;
    STDMETHOD(TotalGet)(QWORD* pqWord) PURE;
    STDMETHOD(ToFileTime)(QWORD* pqWord, FILETIME* pFileTime) PURE;
    STDMETHOD(WaveFormatGet)(SDATA* pdWFEX) PURE;
    STDMETHOD(WaveFormatSet)(SDATA dWFEX) PURE;
};

// Implemented on our side: where the samples actually arrive.
struct __declspec(uuid("{2EC34DA0-C743-11CD-80E5-00AA003E4B50}")) IAudioDest : public IUnknown {
    STDMETHOD(FreeSpace)(DWORD* pdwBytes, BOOL* pfEOF) PURE;
    STDMETHOD(DataSet)(void* pBuffer, DWORD dwSize) PURE;
    STDMETHOD(BookMark)(DWORD dwMarkID) PURE;
};

// Handed to us by the engine through IAudio::PassNotify.
struct __declspec(uuid("{ACB08C00-C743-11CD-80E5-00AA003E4B50}")) IAudioDestNotifySink
    : public IUnknown {
    STDMETHOD(AudioStop)(WORD wReason) PURE;
    STDMETHOD(AudioStart)() PURE;
    STDMETHOD(FreeSpace)(DWORD dwBytes, BOOL fEOF) PURE;
    STDMETHOD(BookMark)(DWORD dwMarkID, BOOL fQueued) PURE;
};

// The class id Ivx230nt.dll's DllGetClassObject accepts. Passing this to the
// dll's own DllGetClassObject is what replaces CoCreateInstance, and with it the
// entire SAPI4 runtime and any need for the engine to be registered.
// {C9C5EDA0-7C89-11D0-0100-000000000000}
constexpr GUID CLSID_InfovoxEngine = {
    0xC9C5EDA0, 0x7C89, 0x11D0, {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

}  // namespace sapi4
}  // namespace ivx
