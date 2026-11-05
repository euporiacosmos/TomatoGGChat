#pragma once
#include <algorithm>
#include <string>
#include <thread>
#include "Util.h"

// exe 옆의 TomatoGGChat.ini 를 읽습니다. 값은 모두 ini 에서 가져오고, ini 에 키가 없을 때만 아래 기본값을 씁니다.
struct AppConfig {
    // [llm]
    std::string  modelPath;               // UTF-8 전체 경로 (model > model_dir+model_file > model_dir 의 첫 *.gguf)
    int          nCtx = 4096;
    int          nThreads = 0;            // Load() 가 항상 ini 의 threads 값(또는 자동 계산값)으로 채운다
    bool         useGpu = true;           // ini use_gpu    : 0 = 항상 CPU
    int          gpuLayers = 999;         // ini gpu_layers : GPU 에 올릴 레이어 수 (999 = 전부)
    int          gpuDevice = -1;          // ini gpu_device : -1 = 자동(전용 GPU 중 여유 메모리 최대), 0.. = 장치 번호
    int          maxNewTokens = 512;
    float        temperature = 0.5f;
    int          topK = 40;
    float        topP = 0.9f;
    int          maxPromptBytes = 7000;   // 대화 기록 길이 상한 (UTF-8 바이트)
    // [ui]
    std::wstring startUrl = L"https://tomato.gg/";
    double       webRatio = 0.62;         // 웹뷰가 차지하는 비율
    // [prompt]
    int          promptBattles = 10;      // 프롬프트에 나열할 최근 전투 수
    int          statBattles = 20;        // 요약 통계에 사용할 최근 전투 수

    static AppConfig Load();
};

namespace cfgdetail {

inline bool IsAbsolute(const std::wstring& p)
{
    return (p.size() > 1 && p[1] == L':') || (!p.empty() && (p[0] == L'\\' || p[0] == L'/'));
}

// dir 안의 첫 번째 *.gguf 파일 이름 (없으면 빈 문자열)
inline std::wstring FindFirstGguf(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.gguf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring name = fd.cFileName;
    FindClose(h);
    return name;
}

} // namespace cfgdetail

inline AppConfig AppConfig::Load()
{
    AppConfig c;
    const std::wstring dir = GetExeDir();
    const std::wstring ini = dir + L"\\TomatoGGChat.ini";
    wchar_t buf[1024];

    auto str = [&](const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
        GetPrivateProfileStringW(sec, key, def, buf, (DWORD)std::size(buf), ini.c_str());
        return std::wstring(buf);
    };
    auto num = [&](const wchar_t* sec, const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(sec, key, def, ini.c_str());
    };
    auto flt = [&](const wchar_t* sec, const wchar_t* key, double def) {
        std::wstring s = str(sec, key, L"");
        return s.empty() ? def : _wtof(s.c_str());
    };

    // ── 모델 경로 ──────────────────────────────────────────────
    //  1) model       = 전체 경로(또는 exe 기준 상대 경로)   : 있으면 이것을 사용
    //  2) model_dir + model_file                              : model_dir 기본값은 "models"
    //  3) model_file 이 비어 있으면 model_dir 의 첫 번째 *.gguf
    std::wstring model = str(L"llm", L"model", L"");
    if (model.empty()) {
        std::wstring mdir = str(L"llm", L"model_dir", L"models");
        if (!cfgdetail::IsAbsolute(mdir)) mdir = dir + L"\\" + mdir;
        std::wstring file = str(L"llm", L"model_file", L"");
        if (file.empty()) file = cfgdetail::FindFirstGguf(mdir);
        model = mdir + L"\\" + (file.empty() ? std::wstring(L"(gguf 파일 없음)") : file);
    } else if (!cfgdetail::IsAbsolute(model)) {
        model = dir + L"\\" + model;
    }
    c.modelPath = ToUtf8(model);

    // ── 스레드: ini 의 threads 값. 0 이하이거나 키가 없으면 코어 수의 절반(최소 2) ──
    const int autoThreads = (std::max)(2, (int)(std::thread::hardware_concurrency() / 2));
    c.nThreads = num(L"llm", L"threads", autoThreads);
    if (c.nThreads <= 0) c.nThreads = autoThreads;

    // ── GPU (Vulkan) ──────────────────────────────────────────
    c.useGpu     = num(L"llm", L"use_gpu", c.useGpu ? 1 : 0) != 0;
    c.gpuLayers  = num(L"llm", L"gpu_layers", c.gpuLayers);
    c.gpuDevice  = num(L"llm", L"gpu_device", c.gpuDevice);

    c.nCtx           = num(L"llm", L"n_ctx", c.nCtx);
    c.maxNewTokens   = num(L"llm", L"max_new_tokens", c.maxNewTokens);
    c.temperature    = (float)flt(L"llm", L"temperature", c.temperature);
    c.topK           = num(L"llm", L"top_k", c.topK);
    c.topP           = (float)flt(L"llm", L"top_p", c.topP);
    c.maxPromptBytes = num(L"llm", L"max_prompt_bytes", c.maxPromptBytes);

    c.startUrl       = str(L"ui", L"start_url", c.startUrl.c_str());
    c.webRatio       = flt(L"ui", L"web_ratio", c.webRatio);
    if (c.webRatio < 0.3) c.webRatio = 0.3;
    if (c.webRatio > 0.85) c.webRatio = 0.85;

    c.promptBattles  = num(L"prompt", L"battles_listed", c.promptBattles);
    c.statBattles    = num(L"prompt", L"battles_for_stats", c.statBattles);
    return c;
}
