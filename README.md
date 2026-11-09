# TomatoGGChat
## 본 프로그램은 Vulkan API 기반으로 구동되어 NVIDIA / AMD / Intel 그래픽카드에서 모두 작동합니다(CUDA는 지원하지 않습니다).
### 📜 라이선스 및 오픈소스 고지 (Open Source Notices)
* **TomatoGGChat**: MIT License
* **llama.cpp**: MIT License (Copyright (c) 2023-2026 Georgi Gerganov)
* **nlohmann/json**: MIT License (Copyright (c) 2013-2026 Niels Lohmann)
* **Microsoft.Web.WebView2**: Microsoft Software License Terms (Copyright (C) Microsoft Corporation)

본 배포 패키지에는 MIT 라이선스 기반의 오픈소스 라이브러리(llama.cpp, nlohmann/json) 및 Microsoft의 소프트웨어 라이선스 약관이 적용되는 Microsoft.Web.WebView2 패키지가 포함되어 있습니다. 각 구성 요소의 상세 라이선스 전문은 동봉된 NOTICE.txt 파일에서 확인하실 수 있습니다.

### 빌드 가이드
*해당 프로젝트는 [2019](https://aka.ms/vs/16/release/vs_community.exe)를 비롯한 구버전의 Visual Studio와 최신 버전인 [2026](https://visualstudio.microsoft.com/ko/downloads/)으로 테스트되지 않았습니다.*
1. 이 레포지토리를 다운로드합니다.
2. [Visual Studio](https://aka.ms/vs/17/release/vs_community.exe)로 연 뒤에 솔루션을 우클릭하고 [NuGet 패키지 복원]을 클릭합니다. Visual Studio Installer에서 [최신 v143 빌드 도구용 C++ 및 MFC(x86 및 x64)]가 체크되어 있어야 합니다.
3. [llama.cpp reasoning-format 기능 추가 이전 버전](https://github.com/ggml-org/llama.cpp/tree/27e8a23300e30cd6ff6107ce262acf832ca60597)을 다운로드합니다. 해당 버전 이상의 [llama.cpp](https://github.com/ggml-org/llama.cpp)을 다운로드하면 [Thinking Tag](https://docs.openwebui.com/features/chat-conversations/chat-features/reasoning-models/)가 답변 출력에 포함됩니다.
4. cmake로 [llama.cpp](https://github.com/ggml-org/llama.cpp/tree/27e8a23300e30cd6ff6107ce262acf832ca60597)을 빌드합니다. Vulkan을 포함하고 빌드하기 때문에 [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)도 설치되어 있는 지 확인합니다.
```
cmake -B build -DGGML_VULKAN=ON
cmake --build build --config Release -j
```
5. 루트 폴더 내에 lib 폴더를 만들고 안에 필요한 .lib 파일을 넣습니다. (ggml.lib, ggml-cpu.lib, ggml-base.lib, llama.lib)
6. 빌드합니다.
7. 빌드 이후에 ~\x64\Debug(또는 Release)\ 이하에 필요한 .dll 파일(ggml.dll, ggml-cpu.dll, ggml-base.dll, ggml-vulkan.dll, llama.dll)을 넣고 models 폴더를 만든 뒤에 ~\x64\Debug(또는 Release)\models\ 이하에 [https://huggingface.co/bartowski/EXAONE-3.5-2.4B-Instruct-GGUF/blob/main/EXAONE-3.5-2.4B-Instruct-IQ4_NL.gguf](https://huggingface.co/bartowski/EXAONE-3.5-2.4B-Instruct-GGUF/blob/main/EXAONE-3.5-2.4B-Instruct-IQ4_NL.gguf) 파일을 넣습니다.
8. 실행합니다.

* .lib 파일은 llama.cpp\build\src\Release\ 폴더와 llama.cpp\build\ggml\src\Release\ 폴더 안에 있습니다.
* .dll 파일은 llama.cpp\build\bin\Release\ 폴더 안에 있습니다.