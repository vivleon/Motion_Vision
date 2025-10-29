#include "pch.h"
#include "MainFrm.h"
#include "FactoryVisionClient.h"
#include "FactoryVisionClientView.h"
#include "SharedData.h"
#include "resource.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include "CCameraSetupDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_MESSAGE(WM_TCP_STATUS, &CMainFrame::OnTcpStatus)
    ON_MESSAGE(WM_CAMERA_DISCONNECTED, &CMainFrame::OnCameraDisconnected)
    ON_MESSAGE(WM_CAMERA_STATUS, &CMainFrame::OnCameraStatus)

    // <<< 상태 표시줄 및 타이머 핸들러 추가 >>>
    ON_MESSAGE(WM_UPDATE_STATUS_PANE, &CMainFrame::OnUpdateStatusPane)
    ON_WM_TIMER()
    // <<< --- 추가 끝 --- >>>

    ON_COMMAND_RANGE(ID_CAMERA_BTN_BASE, ID_CAMERA_BTN_BASE + 63, &CMainFrame::OnCameraViewClick)
    ON_COMMAND(ID_MANUAL_CAPTURE_BTN, &CMainFrame::OnManualCaptureClick)
    ON_COMMAND(ID_SETTINGS_BTN, &CMainFrame::OnSettingsClick)

    ON_COMMAND(ID_VIEW_TOGGLE_LIVEPANEL, &CMainFrame::OnTogglePanel)
    ON_COMMAND(ID_VIEW_DOCK_LEFT, &CMainFrame::OnDockLeft)
    ON_COMMAND(ID_VIEW_DOCK_RIGHT, &CMainFrame::OnDockRight)
    ON_COMMAND(ID_VIEW_FLOAT, &CMainFrame::OnFloatPanel)
    ON_COMMAND(ID_VIEW_HIDE, &CMainFrame::OnHidePanel)
END_MESSAGE_MAP()

// (CMainFrame 생성자는 변경 없음)
CMainFrame::CMainFrame() noexcept
{
}

CMainFrame::~CMainFrame()
{
    // <<< 타이머 종료 >>>
    KillTimer(TIMER_ID_STATUS_UPDATE);
    // <<< --- 종료 끝 --- >>>

    // 앱 종료 직전: 카메라 설정/패널 상태 저장
    SaveConfigs();

    // 카메라 전부 정리 (스레드 종료/소켓 종료 포함)
    m_CameraManager.DisconnectAll();

    // <<< 동적 생성된 버튼 메모리 해제 추가 >>>
    for (CButton* pBtn : m_vecCamButtons) {
        if (pBtn) {
            // 윈도우 핸들이 유효하면(아직 파괴되지 않았다면) 파괴
            if (::IsWindow(pBtn->GetSafeHwnd())) {
                pBtn->DestroyWindow();
            }
            delete pBtn; // CButton 객체 자체 삭제
        }
    }
    m_vecCamButtons.clear();
    // <<< --- 추가 끝 --- >>>
}

// (PreCreateWindow는 변경 없음)
BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
        return FALSE;
    return TRUE;
}


int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // <<< 상태바 초기화 수정 >>>
    InitializeStatusBar();
    // <<< --- 수정 끝 --- >>>

    // UI 폰트 생성
    LOGFONT lf{};
    lf.lfHeight = -16;
    _tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
    m_FontUI.CreateFontIndirect(&lf);

    // 버튼 만들기
    m_btnTogglePanel.Create(_T("패널"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_VIEW_TOGGLE_LIVEPANEL);
    m_btnManualCapture.Create(_T("촬영"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_MANUAL_CAPTURE_BTN);
    m_btnSettings.Create(_T("설정"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_SETTINGS_BTN);

    m_btnTogglePanel.SetFont(&m_FontUI);
    m_btnManualCapture.SetFont(&m_FontUI);
    m_btnSettings.SetFont(&m_FontUI);

    // 설정 불러오기
    LoadConfigs();

    // <<< 카메라 연결 및 상태 초기화 수정 >>>
    m_CameraStatusStrings.fill(_T("Not Used")); // 모든 상태 초기화
    for (size_t i = 0; i < m_CamConfigs.size(); ++i) // ini에 설정된 카메라만
    {
        const CameraConfig& cfg = m_CamConfigs[i];

        // 설정 파일에 있지만 인덱스가 범위를 벗어나는 경우 방지
        if (cfg.nIndex < 0 || cfg.nIndex >= MAX_CAMERAS) continue;

        m_CameraStatusStrings[cfg.nIndex] = _T("Connecting..."); // 연결 시도
        if (m_CameraManager.ConnectCamera(cfg, m_hWnd))
        {
            // 성공 시 스레드가 상태를 업데이트할 때까지 Connecting 유지
            UpdateStatusBarPane(ID_INDICATOR_CAM_BASE + cfg.nIndex, m_CameraStatusStrings[cfg.nIndex]);
        }
        else
        {
            // 연결 실패 시
            m_CameraStatusStrings[cfg.nIndex] = _T("Connect Fail");
            UpdateStatusBarPane(ID_INDICATOR_CAM_BASE + cfg.nIndex, m_CameraStatusStrings[cfg.nIndex]);
        }
    }
    // <<< --- 수정 끝 --- >>>

    // 카메라 버튼 생성 (설정 로드 및 연결 시도 이후)
    CreateDynamicButtonsLayout();

    // LivePanel 탭 구성
    m_LivePanel.BuildTabs(m_CamConfigs);

    // 도킹상태 복원
    switch (m_LivePanel.m_state)
    {
    case CLivePanel::DockLeft:  m_LivePanel.DockLeftPane();  break;
    case CLivePanel::DockRight: m_LivePanel.DockRightPane(); break;
    case CLivePanel::Floating:  m_LivePanel.FloatPane();     break;
    case CLivePanel::Hidden:
    default:
        break;
    }

    m_pView = DYNAMIC_DOWNCAST(CFactoryVisionClientView, GetActiveView());
    if (m_pView) m_pView->SetActiveCamera(0); // 뷰에 기본 카메라 설정

    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());

    // <<< 상태 업데이트용 타이머 시작 >>>
    SetTimer(TIMER_ID_STATUS_UPDATE, 1000, nullptr); // 1초마다
    // <<< --- 시작 끝 --- >>>

    return 0;
}


// <<< 상태 표시줄 초기화 함수 구현 >>>
void CMainFrame::InitializeStatusBar()
{
    if (!m_wndStatusBar.Create(this))
    {
        TRACE0("Failed to create status bar\n");
        return;
    }

    // Pane ID 배열 동적 생성 (ID_INDICATOR_STATUS + MAX_CAMERAS 개)
    UINT* indicators = new UINT[MAX_CAMERAS + 1];
    if (!indicators) {
        TRACE0("Failed to allocate memory for status bar indicators\n");
        return;
    }

    indicators[0] = ID_INDICATOR_STATUS; // 첫 번째 Pane (기본 메시지)
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        indicators[i + 1] = ID_INDICATOR_CAM_BASE + i; // 각 카메라 상태 Pane ID
    }

    // 상태 표시줄에 Pane 설정
    if (!m_wndStatusBar.SetIndicators(indicators, MAX_CAMERAS + 1)) // 총 개수는 MAX_CAMERAS + 1
    {
        TRACE0("Failed to set status bar indicators\n");
        delete[] indicators; // 메모리 해제
        return;
    }

    delete[] indicators; // 배열 사용 끝났으므로 해제

    // 각 Pane의 스타일 및 너비 설정
    // 첫 번째 Pane (ID_INDICATOR_STATUS): 가변 폭
    m_wndStatusBar.SetPaneInfo(0, ID_INDICATOR_STATUS, SBPS_NORMAL | SBPS_STRETCH, 0);

    // 카메라 상태 Pane (ID_INDICATOR_CAM_BASE부터): 고정 폭
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        // Pane 인덱스는 1부터 시작 (0은 ID_INDICATOR_STATUS)
        int nPaneIndex = i + 1;
        UINT nPaneID = ID_INDICATOR_CAM_BASE + i;
        m_wndStatusBar.SetPaneInfo(nPaneIndex, nPaneID, SBPS_NORMAL, 100); // 예: 너비 100

        CString initialText;
        initialText.Format(_T("CAM %d: ---"), i + 1);
        m_wndStatusBar.SetPaneText(nPaneIndex, initialText);
    }
}
// <<< --- 구현 끝 --- >>>

// <<< 특정 Pane 텍스트 업데이트 함수 구현 >>>
void CMainFrame::UpdateStatusBarPane(int nPaneID, const CString& sText)
{
    if (::IsWindow(m_wndStatusBar.GetSafeHwnd())) // 상태바 윈도우 유효성 검사
    {
        // GetPaneInfo 대신 CommandToIndex 사용
        int nPaneIndex = m_wndStatusBar.CommandToIndex(nPaneID);
        if (nPaneIndex != -1) // 유효한 Pane ID인지 확인
        {
            m_wndStatusBar.SetPaneText(nPaneIndex, sText);
        }
        else
        {
            // TRACE(_T("UpdateStatusBarPane: Invalid Pane ID %d\n"), nPaneID);
        }
    }
}
// <<< --- 구현 끝 --- >>>


// ----------------------------------------------------------
// WM_SIZE 들어올 때: 메인 프레임 안에서
// - 상단 버튼 위치
// - LivePanel 위치/폭
// - 중앙 View 위치
// 전부 다시 정렬
// ----------------------------------------------------------
void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);

    if (nType == SIZE_MINIMIZED)
        return;

    UpdateLayout(cx, cy);
}


// ----------------------------------------------------------
// 진짜 레이아웃 계산은 여기서
// cx, cy = 현재 클라이언트 영역 크기
// ----------------------------------------------------------
void CMainFrame::UpdateLayout(int cx, int cy)
{
    if (cx <= 0 || cy <= 0)
        return;

    // 1) 상단 버튼들 배치 영역 계산
    //    topBarHeight 만큼 위쪽에 버튼들 가로로 깔고,
    //    그 아래 영역을 본 화면으로 씀
    const int topBarHeight = 40;
    const int btnW = 70;
    const int btnH = 28;
    const int btnMargin = 8;

    int curX = btnMargin;
    int curY = (topBarHeight - btnH) / 2;

    // 패널 토글 버튼
    m_btnTogglePanel.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 촬영 버튼
    m_btnManualCapture.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 설정 버튼
    m_btnSettings.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 카메라 전환 버튼들 (CAM1, CAM2, ...)
    // UpdateCameraButtons()에서 실제 MoveWindow 호출할 거니까 여기서 호출
    CreateDynamicButtonsLayout(); // 버튼 배열(갯수/텍스트) 보장
    UpdateCameraButtons();        // 위치 재배치

    // 2) 남은 클라이언트 영역(= topBarHeight 아래)을 계산
    CRect rcClient(0, 0, cx, cy);
    CRect rcMainArea = rcClient;
    rcMainArea.top += topBarHeight;

    // 3) LivePanel(도킹 상태면 좌/우 차지)을 위한 rect 계산
    CRect rcPanel = rcMainArea;
    CRect rcView = rcMainArea;

    if (IsLivePanelUsable())  // <- 여기 중요. m_LivePanel 안전할 때만 만진다
    {
        if (m_LivePanel.m_state == CLivePanel::DockLeft)
        {
            rcPanel.right = rcPanel.left + m_LivePanel.m_dockWidth;
            rcView.left = rcPanel.right;
        }
        else if (m_LivePanel.m_state == CLivePanel::DockRight)
        {
            rcPanel.left = rcPanel.right - m_LivePanel.m_dockWidth;
            rcView.right = rcPanel.left;
        }

        // 패널이 Hidden이면 그냥 rcPanel은 무시하고 rcView = rcMainArea 유지

        // 도킹 상태면 MoveWindow/ShowWindow
        if (m_LivePanel.m_state == CLivePanel::DockLeft ||
            m_LivePanel.m_state == CLivePanel::DockRight)
        {
            // 패널 위치/크기 맞추기
            m_LivePanel.MoveWindow(rcPanel);

            // 보이게 (또는 숨김)
            m_LivePanel.ShowWindow(SW_SHOW);

            // 탭/프리뷰 재배치
            m_LivePanel.SafeUpdateLayout();
        }
        else
        {
            // Floating or Hidden이면 메인프레임 영역에는 차지 안 시킴
            // Hidden일 때는 굳이 ShowWindow 안 건드림
        }
    }
    else
    {
        // 패널이 아직 생성중이거나(reparent중) 핸들이 없으면
        // 그냥 전체를 View에 준다
        rcView = rcMainArea;
    }

    // 4) 중앙 View(메인 검사 화면) 위치 갱신
    CFactoryVisionClientView* pView = nullptr;
    pView = m_pView ? m_pView
        : DYNAMIC_DOWNCAST(CFactoryVisionClientView, GetActiveView());

    if (pView && ::IsWindow(pView->GetSafeHwnd()))
    {
        pView->MoveWindow(rcView);
    }

    // 5) 상태바 위치는 MFC가 알아서 처리하니까 여기선 안 건드려도 됨
}


// ----------------------------------------------------------
// 카메라별 버튼들 (CAM1, CAM2 ...) 위치/라벨 갱신
// ----------------------------------------------------------
void CMainFrame::CreateDynamicButtonsLayout()
{
    // m_vecCamButtons 에 CAM1, CAM2... 버튼이 없다면 만든다.
    // 이미 있다면 건너뜀.
    // 버튼 텍스트는 "CAM 1", "CAM 2" 이런 식으로.

    // 갯수 맞추기
    int need = (int)m_CamConfigs.size();
    while ((int)m_vecCamButtons.size() < need)
    {
        int idx = (int)m_vecCamButtons.size();
        auto* pBtn = new CButton;
        CString label;
        if (idx < m_CamConfigs.size() && !m_CamConfigs[idx].sFriendlyName.IsEmpty())
        {
            label = m_CamConfigs[idx].sFriendlyName;
        }
        else
        {
            label.Format(_T("CAM %d"), idx + 1);
        }

        UINT idCmd = ID_CAMERA_BTN_BASE + idx; // ID_CAMERA_BTN_BASE + n

        pBtn->Create(label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(0, 0, 0, 0),
            this,
            idCmd);
        pBtn->SetFont(&m_FontUI);
        m_vecCamButtons.push_back(pBtn);
    }

    // 혹시 카메라 수가 줄었으면 나머지는 숨긴다(폭파까지는 안 함)
    for (size_t i = m_CamConfigs.size(); i < m_vecCamButtons.size(); ++i)
    {
        if (m_vecCamButtons[i] && ::IsWindow(m_vecCamButtons[i]->GetSafeHwnd()))
            m_vecCamButtons[i]->ShowWindow(SW_HIDE);
    }
}

void CMainFrame::UpdateCameraButtons()
{
    const int topBarHeight = 40;
    const int btnH = 28;
    const int btnMargin = 8;
    const int baseX = 8 + (70 + btnMargin) * 3; // 패널/촬영/설정 3개 지난 후 x 시작
    int curX = baseX;
    int curY = (topBarHeight - btnH) / 2;

    for (size_t i = 0; i < m_CamConfigs.size(); ++i)
    {
        if (i >= m_vecCamButtons.size()) break;

        CButton* pBtn = m_vecCamButtons[i];
        if (!pBtn || !::IsWindow(pBtn->GetSafeHwnd()))
            continue;

        // 현재 View에서 보고 있는 카메라면 강조 표시 같은 거 하고 싶으면 여기서
        // (예: 텍스트에 "<Active>" 붙이기 등)

        pBtn->MoveWindow(curX, curY, 70, btnH);
        pBtn->ShowWindow(SW_SHOW);

        curX += 70 + btnMargin;
    }
}


// ----------------------------------------------------------
// "CAM n" 버튼 눌렀을 때 → View의 ActiveCamera 바꿔주기
// ----------------------------------------------------------
void CMainFrame::OnCameraViewClick(UINT nID)
{
    int camIdx = (int)(nID - ID_CAMERA_BTN_BASE); // 0-based index

    if (m_pView && ::IsWindow(m_pView->GetSafeHwnd()))
    {
        m_pView->SetActiveCamera(camIdx);
        m_pView->Invalidate(FALSE);
    }
}


// ----------------------------------------------------------
// "촬영" 버튼 눌렀을 때 → 현재 활성 카메라 한 장 강제 전송 트리거
// ----------------------------------------------------------
void CMainFrame::OnManualCaptureClick()
{
    if (!m_pView) return;

    int activeCam = m_pView->GetActiveCameraIndex();
    if (activeCam < 0) return;

    m_CameraManager.TriggerManualCapture(activeCam);
}


// ----------------------------------------------------------
// "설정" 버튼: 카메라 설정 다이얼로그 (IP/포트/모션) 띄우고 저장 후 재연결
// ----------------------------------------------------------
// ----------------------------------------------------------
// "설정" 버튼: 카메라 설정 다이얼로그 (IP/포트/모션) 띄우고 저장 후 재연결
// ----------------------------------------------------------
void CMainFrame::OnSettingsClick()
{
    // 1. 설정 다이얼로그를 엽니다.
    //    이 다이얼로그는 DoModal()이 끝난 후 (OK 버튼 클릭 시)
    //    CCameraSetupDlg::OnBnClickedSave() 내부에서 
    //    이미 config.ini 파일에 설정을 저장한 상태입니다.
    CCameraSetupDlg dlg(this);
    if (dlg.DoModal() != IDOK)
    {
        return; // 사용자가 '취소'를 눌렀습니다.
    }

    // 2. 다이얼로그에서 설정이 저장되었으므로,
    //    모든 카메라 연결을 끊고 새 설정으로 다시 로드/연결합니다.

    AfxMessageBox(_T("카메라 설정을 변경합니다. 모든 카메라를 다시 연결합니다."));

    // 3. 기존 모든 카메라 연결 해제
    m_CameraManager.DisconnectAll();
    m_vecCamButtons.clear(); // 버튼도 정리 (CreateDynamicButtonsLayout에서 다시 만듦)

    // 4. 새 설정 로드 (config.ini -> m_CamConfigs)
    //    (LoadConfigs 함수가 LoadCameraConfigs를 호출하도록 수정 필요.
    //     아래 5번 항목 참고)
    LoadConfigs();

    // 5. 새 설정으로 카메라 연결
    for (size_t i = 0; i < m_CamConfigs.size(); ++i)
    {
        const CameraConfig& cfg = m_CamConfigs[i];
        m_CameraManager.ConnectCamera(cfg, m_hWnd);
    }

    // 6. LivePanel 탭 재구성
    m_LivePanel.BuildTabs(m_CamConfigs);

    // 7. 메인 UI 레이아웃 (버튼 등) 갱신
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());

    // 8. 뷰 갱신
    if (m_pView)
    {
        m_pView->SetActiveCamera(0); // 첫 번째 카메라로 뷰 리셋
        m_pView->Invalidate(FALSE);
    }
}


// ----------------------------------------------------------
// 패널 토글 / 도킹 전환 관련 핸들러들
// ----------------------------------------------------------
void CMainFrame::OnTogglePanel()
{
    // 숨김 <-> 왼쪽 도킹
    m_LivePanel.TogglePane();

    // 도킹상태 바뀌었을 수 있으니까 레이아웃 업데이트
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnDockLeft()
{
    m_LivePanel.DockLeftPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnDockRight()
{
    m_LivePanel.DockRightPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnFloatPanel()
{
    m_LivePanel.FloatPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnHidePanel()
{
    m_LivePanel.HidePane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}


// ----------------------------------------------------------
// TCP/카메라 상태 메시지 처리 (상태바 업데이트 등)
// ----------------------------------------------------------
LRESULT CMainFrame::OnTcpStatus(WPARAM wParam, LPARAM lParam)
{
    int camIndex = (int)wParam;
    BOOL bConnected = (BOOL)lParam;
    CString sStatus;

    if (camIndex >= 0 && camIndex < MAX_CAMERAS) {
        if (bConnected) {
            // TCP만 연결된 상태. Grab 스레드 상태는 OnCameraStatus나 타이머에서 확인.
            sStatus = _T("TCP OK");
            // m_CameraStatusStrings[camIndex] = sStatus; // 타이머가 덮어쓸 수 있으므로, 일단 보류
        }
        else {
            sStatus = _T("TCP Fail");
            m_CameraStatusStrings[camIndex] = sStatus; // 연결 실패는 확실히 기록
        }

        // PostMessage를 사용하여 메인 스레드에서 상태바 업데이트
        PostMessage(WM_UPDATE_STATUS_PANE, (WPARAM)(ID_INDICATOR_CAM_BASE + camIndex), (LPARAM) new CString(sStatus));
    }
    return 0;
}

LRESULT CMainFrame::OnCameraDisconnected(WPARAM wParam, LPARAM lParam)
{
    int camIndex = (int)wParam;
    CString sStatus = _T("Disconnected");

    if (camIndex >= 0 && camIndex < MAX_CAMERAS) {
        m_CameraStatusStrings[camIndex] = sStatus;
        PostMessage(WM_UPDATE_STATUS_PANE, (WPARAM)(ID_INDICATOR_CAM_BASE + camIndex), (LPARAM) new CString(sStatus));
        // TODO: 필요시 재연결 로직
    }
    return 0;
}

LRESULT CMainFrame::OnCameraStatus(WPARAM wParam, LPARAM lParam)
{
    int camIndex = (int)wParam;
    CString* pStatus = (CString*)lParam; // new CString으로 전달됨

    if (camIndex >= 0 && camIndex < MAX_CAMERAS && pStatus) {
        m_CameraStatusStrings[camIndex] = *pStatus;
        // PostMessage로 전달 (pStatus의 소유권도 함께 전달)
        PostMessage(WM_UPDATE_STATUS_PANE, (WPARAM)(ID_INDICATOR_CAM_BASE + camIndex), (LPARAM)pStatus);
    }
    else {
        delete pStatus; // 잘못된 경우 메모리 해제
    }
    return 0;
}

// <<< 상태 표시줄 업데이트 메시지 핸들러 구현 >>>
LRESULT CMainFrame::OnUpdateStatusPane(WPARAM wParam, LPARAM lParam)
{
    int nPaneID = (int)wParam; // Pane ID
    CString* pStatus = (CString*)lParam; // 상태 문자열 포인터 (new로 할당됨)

    if (pStatus)
    {
        UpdateStatusBarPane(nPaneID, *pStatus);
        delete pStatus; // <<< 여기서 CString 객체 해제
    }
    return 0;
}
// <<< --- 구현 끝 --- >>>

// <<< 타이머 핸들러 구현 >>>
void CMainFrame::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_ID_STATUS_UPDATE)
    {
        // 설정된 카메라 수 만큼 주기적으로 상태 확인
        for (int i = 0; i < (int)m_CamConfigs.size(); ++i)
        {
            int camIndex = m_CamConfigs[i].nIndex; // 실제 카메라 인덱스
            if (camIndex < 0 || camIndex >= MAX_CAMERAS) continue;

            CString currentStatusText = _T("Unknown");
            bool isConnected = m_CameraManager.IsCameraConnected(camIndex);
            bool isGrabbing = false;

            try {
                // IsCameraConnected가 true일 때만 IsGrabbing 시도
                if (isConnected && m_CameraManager.m_Cameras[camIndex].IsOpen()) {
                    isGrabbing = m_CameraManager.m_Cameras[camIndex].IsGrabbing();
                }
            }
            catch (const Pylon::GenericException& e) {
                // IsGrabbing() 호출 중 예외 발생 가능성 (예: 장치 제거)
                TRACE(_T("Exception checking IsGrabbing for Cam %d: %s\n"), camIndex, CString(e.GetDescription()));
                isConnected = false; // 예외 발생 시 연결 끊김으로 간주
                isGrabbing = false;
                // 연결 끊김 메시지 처리
                PostMessage(WM_CAMERA_DISCONNECTED, (WPARAM)camIndex, 0);
            }
            catch (...) {
                TRACE(_T("Unknown exception checking IsGrabbing for Cam %d\n"), camIndex);
                isConnected = false;
                isGrabbing = false;
                PostMessage(WM_CAMERA_DISCONNECTED, (WPARAM)camIndex, 0);
            }

            // 상태 문자열 결정
            if (isGrabbing) {
                currentStatusText = _T("Grabbing"); // 1순위: Grab 중
            }
            else if (isConnected) {
                currentStatusText = _T("Connected"); // 2순위: 연결됨 (Grab 안함)
            }
            else {
                // 3순위: 이전에 설정된 오류 상태 (예: "TCP Fail", "Disconnected") 유지
                // 또는 기본값 "Not Connected"
                if (m_CameraStatusStrings[camIndex] == _T("Connecting...") ||
                    m_CameraStatusStrings[camIndex] == _T("Grabbing") ||
                    m_CameraStatusStrings[camIndex] == _T("Connected") ||
                    m_CameraStatusStrings[camIndex] == _T("Not Used")) // 이전 상태가 오류가 아니었다면
                {
                    currentStatusText = _T("Not Connected");
                }
                else // "TCP Fail", "Disconnected" 등 오류 상태 유지
                {
                    currentStatusText = m_CameraStatusStrings[camIndex];
                }
            }

            // 상태가 변경되었을 때만 업데이트
            if (m_CameraStatusStrings[camIndex] != currentStatusText) {
                m_CameraStatusStrings[camIndex] = currentStatusText;
                // PostMessage로 상태바 업데이트 요청
                PostMessage(WM_UPDATE_STATUS_PANE, (WPARAM)(ID_INDICATOR_CAM_BASE + camIndex), (LPARAM) new CString(currentStatusText));
            }

            // 카메라 버튼 활성화/비활성화
            // m_vecCamButtons는 m_CamConfigs 순서대로 생성됨
            if (i < (int)m_vecCamButtons.size() && m_vecCamButtons[i] && ::IsWindow(m_vecCamButtons[i]->GetSafeHwnd())) {
                // Grab 중일 때만 버튼 활성화 (또는 isConnected일 때 활성화 - 정책에 따라)
                m_vecCamButtons[i]->EnableWindow(isGrabbing);
            }
        }
    }

    CFrameWnd::OnTimer(nIDEvent);
}
// ----------------------------------------------------------
// 설정 불러오기 / 저장하기 (ini)
// ----------------------------------------------------------
void CMainFrame::LoadConfigs()
{
    // config.ini 에서 카메라 설정(m_CamConfigs)과
    // 패널 설정(m_LivePanel)을 불러옵니다.
    // (LoadCameraConfigs 함수가 패널 설정도 같이 로드함)
    LoadCameraConfigs(m_CamConfigs);
}

void CMainFrame::SaveConfigs()
{
    // 현재 m_CamConfigs, m_LivePanel 상태(DockState, 위치 등)를
    // config.ini로 저장합니다.
    SaveCameraConfigs(m_CamConfigs);
}

CString CMainFrame::GetIniPath() const
{
    TCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, exePath, MAX_PATH);

    CString folder(exePath);
    int slash = folder.ReverseFind(_T('\\'));
    if (slash >= 0)
        folder = folder.Left(slash);

    // exe 폴더 밑에 config.ini 사용
    CString iniPath = folder + _T("\\config.ini");
    return iniPath;
}


BOOL CMainFrame::LoadCameraConfigs(std::vector<CameraConfig>& out)
{
    out.clear();

    CString ini = GetIniPath();
    TCHAR buf[256]{};

    // 카메라 슬롯은 1부터 MAX_CAMERAS까지 "CAM1", "CAM2", ... 이런 섹션으로 관리
    for (int i = 0; i < MAX_CAMERAS; ++i)
    {
        CString sec;
        sec.Format(_T("CAM%d"), i + 1);

        // 시리얼 가져와서 없으면 이 슬롯은 건너뛴다 (빈 슬롯 취급)
        GetPrivateProfileString(sec, _T("Serial"), _T(""), buf, _countof(buf), ini);
        CString serial = buf;
        if (serial.IsEmpty())
            continue;

        CameraConfig cfg{};
        cfg.nIndex = i;

        cfg.sSerial = serial;

        // FriendlyName: ini에 없으면 "CAM X" 형태로 기본 세팅
        GetPrivateProfileString(sec, _T("Name"), _T(""), buf, _countof(buf), ini);
        if (buf[0] != 0)
            cfg.sFriendlyName = buf;
        else
        {
            cfg.sFriendlyName.Format(_T("CAM %d"), i + 1);
        }

        // IP
        GetPrivateProfileString(sec, _T("IP"), _T("127.0.0.1"), buf, _countof(buf), ini);
        cfg.sIp = buf;

        // Port
        cfg.nPort = GetPrivateProfileInt(sec, _T("Port"), 9000 + i, ini);

        // MotionEnable / Threshold
        cfg.bMotionEnabled = GetPrivateProfileInt(sec, _T("MotionEnable"), 1, ini);
        cfg.nMotionThreshold = GetPrivateProfileInt(sec, _T("Threshold"), 5000, ini);

        // <<< 상세 설정 로드 >>>
        // GetPrivateProfileString으로 읽고 _ttof 사용하여 double로 변환
        GetPrivateProfileString(sec, _T("ExposureTime"), _T("10000.0"), buf, _countof(buf), ini);
        cfg.dExposureTime = _ttof(buf);

        GetPrivateProfileString(sec, _T("Gain"), _T("1.0"), buf, _countof(buf), ini);
        cfg.dGain = _ttof(buf);
		// <<< 상세 설정 로드 끝 >>>

        out.push_back(cfg);
    }

    // 패널 상태 복원 (도킹/플로팅 정보 등)
    {
        CString secPanel = _T("Panel");

        // DockState
        GetPrivateProfileString(secPanel, _T("DockState"), _T("Hidden"), buf, _countof(buf), ini);
        CLivePanel::DockState st = CLivePanel::Hidden;
        if (_tcscmp(buf, _T("Left")) == 0)       st = CLivePanel::DockLeft;
        else if (_tcscmp(buf, _T("Right")) == 0) st = CLivePanel::DockRight;
        else if (_tcscmp(buf, _T("Floating")) == 0) st = CLivePanel::Floating;
        else if (_tcscmp(buf, _T("Hidden")) == 0)   st = CLivePanel::Hidden;

        // DockWidth
        int dockW = GetPrivateProfileInt(secPanel, _T("DockWidth"), 380, ini);

        // FloatRect "L,T,R,B"
        GetPrivateProfileString(secPanel, _T("FloatRect"),
            _T("100,100,460,620"), buf, _countof(buf), ini);
        int L = 100, T = 100, R = 460, B = 620;
        _stscanf_s(buf, _T("%d,%d,%d,%d"), &L, &T, &R, &B);
        CRect floatRc(L, T, R, B);

        // PreviewFpsCap
        int fpsCap = GetPrivateProfileInt(secPanel, _T("PreviewFpsCap"), 12, ini);

        // JpegQuality
        int jpegQ = GetPrivateProfileInt(secPanel, _T("JpegQuality"), 92, ini);

        // LivePanel 쪽에 적용
        m_LivePanel.ApplyConfig(dockW, st, floatRc, fpsCap, jpegQ);
    }

    return TRUE;
}


BOOL CMainFrame::SaveCameraConfigs(const std::vector<CameraConfig>& cfgs)
{
    CString ini = GetIniPath();

    // <<< 기존 카메라 섹션 클리어 (선택 사항, 중복 방지) >>>
    // 예를 들어, CAM1 ~ CAM_MAX 까지의 섹션을 지우는 로직 추가 가능
    // WritePrivateProfileSection(_T("CAM1"), NULL, ini); // 이런 식으로 반복

    for (const auto& cfg : cfgs)
    {
        CString sec;
        sec.Format(_T("CAM%d"), cfg.nIndex + 1);

        WritePrivateProfileString(sec, _T("Serial"), cfg.sSerial, ini);
        WritePrivateProfileString(sec, _T("Name"), cfg.sFriendlyName, ini);
        WritePrivateProfileString(sec, _T("IP"), cfg.sIp, ini);

        CString v; // 임시 문자열 버퍼
        v.Format(_T("%d"), cfg.nPort);
        WritePrivateProfileString(sec, _T("Port"), v, ini);

        v.Format(_T("%d"), cfg.bMotionEnabled ? 1 : 0);
        WritePrivateProfileString(sec, _T("MotionEnable"), v, ini);

        v.Format(_T("%d"), cfg.nMotionThreshold);
        WritePrivateProfileString(sec, _T("Threshold"), v, ini);

        // <<< 상세 설정 저장 >>>
        // double 값을 문자열로 변환하여 저장 (소수점 정밀도 지정 가능)
        v.Format(_T("%.1f"), cfg.dExposureTime); // 예: 소수점 첫째 자리까지
        WritePrivateProfileString(sec, _T("ExposureTime"), v, ini);

        v.Format(_T("%.2f"), cfg.dGain); // 예: 소수점 둘째 자리까지
        WritePrivateProfileString(sec, _T("Gain"), v, ini);
        // <<< --- 저장 끝 --- >>>
    }

    // <<< cfgs에 없는 인덱스의 섹션 제거 (카메라 삭제 시) >>>
    std::vector<bool> saved(MAX_CAMERAS, false);
    for (const auto& cfg : cfgs) {
        if (cfg.nIndex >= 0 && cfg.nIndex < MAX_CAMERAS) {
            saved[cfg.nIndex] = true;
        }
    }
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        if (!saved[i]) {
            CString sec;
            sec.Format(_T("CAM%d"), i + 1);
            WritePrivateProfileSection(sec, NULL, ini); // 섹션 내용 전체 삭제
        }
    }
    // <<< --- 제거 끝 --- >>>
    // 패널 상태도 같이 저장해주면 깔끔
    {
        CString secPanel = _T("Panel");

        // DockState 문자열화
        CString dockStr = _T("Hidden");
        switch (m_LivePanel.m_state)
        {
        case CLivePanel::DockLeft:   dockStr = _T("Left");      break;
        case CLivePanel::DockRight:  dockStr = _T("Right");     break;
        case CLivePanel::Floating:   dockStr = _T("Floating");  break;
        case CLivePanel::Hidden:
        default:                     dockStr = _T("Hidden");    break;
        }
        WritePrivateProfileString(secPanel, _T("DockState"), dockStr, ini);

        // DockWidth
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_dockWidth);
            WritePrivateProfileString(secPanel, _T("DockWidth"), v, ini);
        }

        // FloatRect
        {
            CRect r = m_LivePanel.m_floatRect;
            CString v;
            v.Format(_T("%d,%d,%d,%d"), r.left, r.top, r.right, r.bottom);
            WritePrivateProfileString(secPanel, _T("FloatRect"), v, ini);
        }

        // PreviewFpsCap
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_previewFpsCap);
            WritePrivateProfileString(secPanel, _T("PreviewFpsCap"), v, ini);
        }

        // JpegQuality
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_jpegQuality);
            WritePrivateProfileString(secPanel, _T("JpegQuality"), v, ini);
        }
    }

    return TRUE;
}

// ----------------------------------------------------------
// 디버그 유틸 (원래 있던 거 그대로 유지)
// ----------------------------------------------------------
#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
    CFrameWnd::AssertValid();
}
void CMainFrame::Dump(CDumpContext& dc) const
{
    CFrameWnd::Dump(dc);
}
#endif
