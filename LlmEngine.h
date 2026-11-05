#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "llama.h"
#include "ggml-backend.h"
#include "Models.h"

struct LlmParams {
    std::string modelPath;    // UTF-8
    int   nCtx     = 4096;
    int   nThreads = 0;       // 0 = 자동
    float temp     = 0.6f;
    int   topK     = 40;
    float topP     = 0.9f;
    // ── GPU 가속 (Vulkan 등 ggml 백엔드) ──
    bool  useGpu     = true;   // false 면 항상 CPU
    int   nGpuLayers = 999;    // GPU 에 올릴 레이어 수 (999 = 전부)
    int   gpuDevice  = -1;     // -1 = 자동(전용 GPU 중 여유 메모리 최대), 0.. = 로딩 시 출력되는 장치 목록의 번호
};

// llama.cpp 래퍼. 로딩/추론 모두 별도 스레드에서 수행하고 PostMessage 로 UI 에 알린다.
class CLlmEngine
{
public:
    ~CLlmEngine() { Shutdown(); }

    void LoadAsync(const LlmParams& p, HWND notify);                       // → WM_LLM_LOADED
    bool IsReady() const { return m_ready; }
    bool IsBusy()  const { return m_busy; }
    bool GenerateAsync(std::vector<ChatMsg> msgs, HWND notify, int maxNew); // → WM_LLM_TOKEN* / WM_LLM_DONE
    void Cancel() { m_cancel = true; }
    void Shutdown();

private:
    bool LoadSync(const LlmParams& p, std::wstring& msg);                // msg: 성공 시 장치 안내, 실패 시 오류
    bool TryLoad(const LlmParams& p, ggml_backend_dev_t gpu, std::wstring& err);   // gpu==nullptr 이면 CPU
    void FreeAll();
    std::string ApplyTemplate(const std::vector<ChatMsg>& msgs);
    void Run(std::vector<ChatMsg> msgs, HWND hwnd, int maxNew);
    void Finish(HWND hwnd, bool cancelled);

    llama_model*       m_model = nullptr;
    llama_context*     m_ctx   = nullptr;
    const llama_vocab* m_vocab = nullptr;
    llama_sampler*     m_smpl  = nullptr;
    int                m_nCtx  = 0;

    std::thread        m_loadTh, m_genTh;
    std::atomic<bool>  m_ready{false}, m_busy{false}, m_cancel{false};
};
