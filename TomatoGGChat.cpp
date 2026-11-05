
// TomatoGGChat.cpp: 애플리케이션에 대한 클래스 동작을 정의합니다.
//

#include "pch.h"
#include "framework.h"
#include "TomatoGGChat.h"
#include "TomatoGGChatDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CTomatoGGChatApp

BEGIN_MESSAGE_MAP(CTomatoGGChatApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CTomatoGGChatApp 생성

CTomatoGGChatApp::CTomatoGGChatApp()
{
	// TODO: 여기에 생성 코드를 추가합니다.
	// InitInstance에 모든 중요한 초기화 작업을 배치합니다.
}


// 유일한 CTomatoGGChatApp 개체입니다.

CTomatoGGChatApp theApp;


// CTomatoGGChatApp 초기화

BOOL CTomatoGGChatApp::InitInstance()
{
	INITCOMMONCONTROLSEX icc = {};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icc);
	CWinApp::InitInstance();

	// MFC 컨트롤의 테마를 사용하기 위해 "Windows 원형" 비주얼 관리자 활성화
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// WebView2 는 STA COM 이 필요하다. (AfxOleInit = OleInitialize)
	if (!AfxOleInit()) {
		AfxMessageBox(L"OLE 초기화에 실패했습니다.");
		return FALSE;
	}
	AfxEnableControlContainer();
	AfxInitRichEdit2();                 // CRichEditCtrl (Rich Edit 2.0 컨트롤)

	SetRegistryKey(L"TomatoGGChat");

	CTomatoGGChatDlg dlg;
	m_pMainWnd = &dlg;
	dlg.DoModal();

	// 대화 상자가 닫혔으므로 응용 프로그램의 메시지 펌프를 시작하지 않고 응용 프로그램을 끝낼 수 있도록 FALSE를
	// 반환합니다.
	return FALSE;
}

