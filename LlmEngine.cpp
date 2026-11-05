#include "pch.h"
#include "LlmEngine.h"
#include "Message.h"
#include "Util.h"
#include "ggml-backend.h"
#include <format>

// ─────────────────────────────────────────────────────────────
// 주의: llama.cpp 의 C API 는 버전마다 바뀝니다. 이 파일은 최근 API
// (llama_vocab, llama_memory_clear, llama_model_load_from_file ...) 기준입니다.
// 빌드 오류가 나면 받아 둔 llama.h 의 시그니처에 맞춰 해당 줄만 고치세요.
// ─────────────────────────────────────────────────────────────

// ── GPU 장치 선택 ─────────────────────────────────────────────
//  · gpu_device >= 0 : 로딩 시 출력되는 목록의 번호로 직접 지정
//  · -1 (자동)       : 전용 GPU(dedicated memory) 중 여유 메모리가 가장 큰 것, 없으면 내장 GPU
//  (GGML_BACKEND_DEVICE_TYPE_IGPU 는 최근 ggml 에만 있습니다. 오래된 빌드라면 igpus 관련 줄만 지우세요.)
static ggml_backend_dev_t PickGpuDevice(int wanted, std::wstring& log)
{
    std::vector<ggml_backend_dev_t> gpus, igpus;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const auto type = ggml_backend_dev_type(dev);
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU)       gpus.push_back(dev);
    }
    std::vector<ggml_backend_dev_t> all = gpus;
    all.insert(all.end(), igpus.begin(), igpus.end());
    if (all.empty()) return nullptr;

    log += L"사용 가능한 GPU:\n";
    for (size_t i = 0; i < all.size(); ++i) {
        size_t freeB = 0, totalB = 0;
        ggml_backend_dev_memory(all[i], &freeB, &totalB);
        log += std::format(L"  [{}] {} - {} (총 {} MB, 여유 {} MB){}\n", i,
                           FromUtf8(ggml_backend_dev_name(all[i])), FromUtf8(ggml_backend_dev_description(all[i])),
                           totalB >> 20, freeB >> 20, i >= gpus.size() ? L" [내장]" : L"");
    }

    if (wanted >= 0) {
        if ((size_t)wanted < all.size()) return all[wanted];
        log += std::format(L"gpu_device={} 번 장치가 없어 자동으로 고릅니다.\n", wanted);
    }
    const auto& pool = gpus.empty() ? igpus : gpus;
    ggml_backend_dev_t best = pool[0];
    size_t bestFree = 0;
    for (auto d : pool) {
        size_t f = 0, t = 0;
        ggml_backend_dev_memory(d, &f, &t);
        if (f > bestFree) { bestFree = f; best = d; }
    }
    return best;
}

void CLlmEngine::LoadAsync(const LlmParams& p, HWND notify)
{
    if (m_loadTh.joinable()) return;
    m_loadTh = std::thread([this, p, notify] {
        std::wstring msg;
        bool ok = LoadSync(p, msg);
        m_ready = ok;
        // 성공이면 장치 안내, 실패면 오류 문구를 LPARAM 으로 보낸다. (UI 에서 delete)
        PostMessageW(notify, WM_LLM_LOADED, ok ? 1 : 0, (LPARAM) new std::wstring(msg));
    });
}

void CLlmEngine::FreeAll()
{
    if (m_smpl)  { llama_sampler_free(m_smpl);  m_smpl  = nullptr; }
    if (m_ctx)   { llama_free(m_ctx);           m_ctx   = nullptr; }
    if (m_model) { llama_model_free(m_model);   m_model = nullptr; }
    m_vocab = nullptr;
}

// 모델 + 컨텍스트 + 샘플러를 만든다. gpu 가 있으면 그 장치 하나에만 올리고, nullptr 이면 CPU 로만 돌린다.
bool CLlmEngine::TryLoad(const LlmParams& p, ggml_backend_dev_t gpu, std::wstring& err)
{
    llama_model_params mp = llama_model_default_params();
    ggml_backend_dev_t devs[2] = { gpu, nullptr };               // NULL 로 끝나는 장치 목록
    static ggml_backend_dev_t noDevs[1] = { nullptr };           // 빈 목록 = 어떤 GPU 도 쓰지 않음
    if (gpu) {
        mp.devices = devs;
        mp.n_gpu_layers = p.nGpuLayers;
        mp.split_mode = LLAMA_SPLIT_MODE_NONE;                   // 한 장치에만 올린다
        mp.main_gpu = 0;                                         // devices 목록에서의 번호
    } else {
        mp.devices = noDevs;
        mp.n_gpu_layers = 0;
    }

    m_model = llama_model_load_from_file(p.modelPath.c_str(), mp);
    if (!m_model) { err = L"모델을 불러오지 못했습니다: " + FromUtf8(p.modelPath); return false; }
    m_vocab = llama_model_get_vocab(m_model);

    const int threads = p.nThreads > 0 ? p.nThreads
                                       : (std::max)(2, (int)(std::thread::hardware_concurrency() / 2));
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = m_nCtx = p.nCtx;
    cp.n_batch = p.nCtx;
    cp.n_threads = cp.n_threads_batch = threads;
    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) { err = L"llama 컨텍스트 생성에 실패했습니다. (GPU 메모리 부족일 수 있어요)"; FreeAll(); return false; }

    m_smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(m_smpl, llama_sampler_init_top_k(p.topK));
    llama_sampler_chain_add(m_smpl, llama_sampler_init_top_p(p.topP, 1));
    llama_sampler_chain_add(m_smpl, llama_sampler_init_temp(p.temp));
    llama_sampler_chain_add(m_smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return true;
}

bool CLlmEngine::LoadSync(const LlmParams& p, std::wstring& msg)
{
    static std::once_flag once;
    std::call_once(once, [] {
        ggml_backend_load_all();      // 공유 DLL 빌드: ggml-vulkan.dll, ggml-cpu*.dll 등 exe 옆의 백엔드 로드
        llama_backend_init();
        llama_log_set([](ggml_log_level lvl, const char* text, void*) {
            if (lvl >= GGML_LOG_LEVEL_WARN) OutputDebugStringA(text);
        }, nullptr);
    });

    std::wstring log;                                            // 사용자에게 보여줄 안내
    ggml_backend_dev_t gpu = nullptr;
    if (p.useGpu) {
        gpu = PickGpuDevice(p.gpuDevice, log);
        if (!gpu) log += L"사용 가능한 GPU 장치를 찾지 못했어요. (Vulkan 빌드 여부, 그래픽 드라이버, ggml-vulkan.dll 위치 확인)\n";
    }

    std::wstring err, mode;
    bool ok = false;
    if (gpu) {
        ok = TryLoad(p, gpu, err);
        if (ok) {
            mode = std::format(L"GPU 가속 사용: {} ({}), GPU 레이어 {}", FromUtf8(ggml_backend_dev_name(gpu)),
                               FromUtf8(ggml_backend_dev_description(gpu)),
                               p.nGpuLayers >= 999 ? std::wstring(L"전체") : std::to_wstring(p.nGpuLayers));
        } else {
            log += L"GPU 로 불러오지 못해 CPU 로 다시 시도합니다: " + err + L"\n";   // VRAM 부족 등
            FreeAll();
        }
    }
    if (!ok) {
        ok = TryLoad(p, nullptr, err);
        if (ok) mode = p.useGpu ? L"CPU 로 실행합니다." : L"CPU 로 실행합니다. (use_gpu=0)";
    }
    if (!ok) { msg = log + err; return false; }

    msg = log + mode;
    return true;
}

std::string CLlmEngine::ApplyTemplate(const std::vector<ChatMsg>& msgs)
{
    std::vector<llama_chat_message> cm;
    for (auto& m : msgs) cm.push_back({ m.role.c_str(), m.content.c_str() });

    const char* tmpl = llama_model_chat_template(m_model, nullptr);
    std::vector<char> buf(1 << 16);
    int n = tmpl ? llama_chat_apply_template(tmpl, cm.data(), cm.size(), true, buf.data(), (int)buf.size()) : -1;
    if (n > (int)buf.size()) {
        buf.resize(n);
        n = llama_chat_apply_template(tmpl, cm.data(), cm.size(), true, buf.data(), (int)buf.size());
    }
    if (n >= 0) return std::string(buf.data(), n);

    // 폴백: 내장 템플릿 매칭에 실패(-1)하면 EXAONE 형식으로 수동 구성한다.
    //  ★ 반드시 GGUF 메타데이터의 chat_template 과 대조해 보세요.
    //  (EXAONE 4.0 은 thinking 비활성 시 "[|assistant|]" 뒤에 "<think>\n\n</think>\n\n" 이 붙는 템플릿일 수 있음)
    std::string s;
    for (auto& m : msgs)
        s += "[|" + m.role + "|]" + m.content + (m.role == "user" ? "\n" : "[|endofturn|]\n");
    s += "[|assistant|]";
    return s;
}

// 토큰이 UTF-8 멀티바이트(한글 3바이트) 중간에서 잘릴 수 있으므로, 완결된 부분의 길이만 돌려준다.
static size_t CompleteUtf8Len(const std::string& s)
{
    const size_t n = s.size();
    for (size_t back = 1; back <= 3 && back <= n; ++back) {
        unsigned char c = (unsigned char)s[n - back];
        if ((c & 0xC0) == 0x80) continue;                                   // 연속 바이트
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        return need > back ? n - back : n;
    }
    return n;
}

static void PostText(HWND h, const std::string& u8)
{
    if (u8.empty()) return;
    auto* w = new std::wstring(FromUtf8(u8));
    PostMessageW(h, WM_LLM_TOKEN, (WPARAM)w, 0);       // UI 에서 delete
}

bool CLlmEngine::GenerateAsync(std::vector<ChatMsg> msgs, HWND hwnd, int maxNew)
{
    if (!m_ready) return false;
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true)) return false;
    if (m_genTh.joinable()) m_genTh.join();
    m_cancel = false;
    m_genTh = std::thread([this, msgs = std::move(msgs), hwnd, maxNew]() mutable { Run(std::move(msgs), hwnd, maxNew); });
    return true;
}

void CLlmEngine::Finish(HWND hwnd, bool cancelled)
{
    m_busy = false;                                    // DONE 을 받은 UI 가 바로 재요청해도 되도록 먼저 해제
    PostMessageW(hwnd, WM_LLM_DONE, cancelled ? 1 : 0, 0);
}

void CLlmEngine::Run(std::vector<ChatMsg> msgs, HWND hwnd, int maxNew)
{
    const std::string prompt = ApplyTemplate(msgs);
    llama_sampler_reset(m_smpl);

    int n = -llama_tokenize(m_vocab, prompt.c_str(), (int)prompt.size(), nullptr, 0, true, true);
    if (n <= 0) {
        PostMessageW(hwnd, WM_LLM_ERROR, (WPARAM) new std::wstring(L"토큰화에 실패했습니다."), 0);
        Finish(hwnd, false);
        return;
    }
    std::vector<llama_token> toks(n);
    llama_tokenize(m_vocab, prompt.c_str(), (int)prompt.size(), toks.data(), n, true, true);

    if (n + maxNew > m_nCtx) {
        PostMessageW(hwnd, WM_LLM_ERROR, (WPARAM) new std::wstring(
            std::format(L"프롬프트가 너무 깁니다 ({}토큰). /clear 로 대화를 비우거나 ini 의 n_ctx 를 늘려 주세요.", n)), 0);
        Finish(hwnd, false);
        return;
    }

    llama_batch batch = llama_batch_get_one(toks.data(), (int)toks.size());
    llama_token tok = 0;
    std::string pending;

    for (int i = 0; i < maxNew && !m_cancel; ++i) {
        if (llama_decode(m_ctx, batch) != 0) {
            PostMessageW(hwnd, WM_LLM_ERROR, (WPARAM) new std::wstring(L"llama_decode 실패"), 0);
            break;
        }
        tok = llama_sampler_sample(m_smpl, m_ctx, -1);
        if (llama_vocab_is_eog(m_vocab, tok)) break;

        char piece[256];
        int len = llama_token_to_piece(m_vocab, tok, piece, (int)sizeof(piece), 0, true);
        if (len > 0) pending.append(piece, len);

        size_t ok = CompleteUtf8Len(pending);
        PostText(hwnd, pending.substr(0, ok));
        pending.erase(0, ok);

        batch = llama_batch_get_one(&tok, 1);
    }
    Finish(hwnd, m_cancel.load());
}

void CLlmEngine::Shutdown()
{
    m_cancel = true;
    if (m_loadTh.joinable()) m_loadTh.join();
    if (m_genTh.joinable())  m_genTh.join();
    FreeAll();
    m_ready = false;
}
