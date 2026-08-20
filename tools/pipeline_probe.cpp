// Console probe: prints what the text pipeline hands each engine, so its output can be
// diffed against tools/translit_ref.py and fed straight into the real dll.
#include <cstdio>
#include <string>
#include <windows.h>
#include "text_pipeline.hpp"

using namespace Bestspeech;

static void put_utf8(const std::wstring& w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    fwrite(s.data(), 1, s.size(), stdout);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: pipeline_probe <engine-id> <text>\n");
        return 2;
    }
    char id[32] = {};
    WideCharToMultiByte(CP_ACP, 0, argv[1], -1, id, sizeof(id) - 1, nullptr, nullptr);
    const int e = engine_by_id(id);
    if (e < 0) {
        fprintf(stderr, "unknown engine %s\n", id);
        return 2;
    }
    put_utf8(text::prepare(argv[2], engines[e]));
    fputc('\n', stdout);
    return 0;
}
