#pragma once
// 워커 스레드 → UI 스레드 통지용 사용자 정의 메시지
#define WM_LLM_LOADED (WM_APP + 1)   // WPARAM: 1=성공 0=실패, LPARAM: 실패 시 new std::wstring* (UI에서 delete)
#define WM_LLM_TOKEN  (WM_APP + 2)   // WPARAM: new std::wstring* (UI에서 delete)
#define WM_LLM_DONE   (WM_APP + 3)   // WPARAM: 1=취소됨
#define WM_LLM_ERROR  (WM_APP + 4)   // WPARAM: new std::wstring* (UI에서 delete)