#pragma once
#include "CCameraManager.h"
#include "CLivePanel.h"
#include "SharedData.h"
#include <vector>
#include <array> // <<< std::array 사용

// <<< 상태 표시줄 Pane ID 정의 (Resource.h와 중복되지 않도록) >>>
// (주의: 이 ID들은 Resource.h가 아닌 MainFrm.h에서만 사용)
#define ID_INDICATOR_STATUS 1 // 첫 번째 Pane (기본 메시지용 ID)
#define ID_INDICATOR_CAM_BASE 2 // 카메라 상태 Pane 시작 ID
// <<< --- 정의 끝 --- >>>

// <<< 타이머 ID 정의 >>>
#define TIMER_ID_STATUS_UPDATE 1
// <<< --- 정의 끝 --- >>>


class CMainFrame : public CFrameWnd
{
    DECLARE_DYNCREATE(CMainFrame)
protected:
    CMainFrame() noexcept;

public:
    virtual ~CMainFrame();
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

    // === 공개 getter ===
    CCameraManager* GetCameraManager() { return &m_CameraManager; }
    CLivePanel* GetLivePanel() { return &m_LivePanel; }

    BOOL LoadCameraConfigs(std::vector<CameraConfig>& out);
    BOOL SaveCameraConfigs(const std::vector<CameraConfig>& cfgs);

protected:
    CStatusBar                 m_wndStatusBar;
    class CFactoryVisionClientView* m_pView = nullptr;
    CCameraManager             m_CameraManager;
    std::vector<CameraConfig>  m_CamConfigs;

    CLivePanel                 m_LivePanel;
    CFont                      m_FontUI;
    CButton                    m_btnTogglePanel, m_btnManualCapture, m_btnSettings;
    std::vector<CButton*>      m_vecCamButtons;

    // <<< 카메라 상태 저장을 위한 배열 추가 >>>
    std::array<CString, MAX_CAMERAS> m_CameraStatusStrings;
    // <<< --- 추가 끝 --- >>>

    // === LivePanel 안전 검사 헬퍼 ===
    bool IsLivePanelUsable() const
    {
        if (!::IsWindow(m_LivePanel.GetSafeHwnd()))
            return false;
        if (!m_LivePanel.IsStableForLayout())
            return false;
        return true;
    }

    // 레이아웃
    void UpdateLayout(int cx, int cy);
    void CreateDynamicButtonsLayout();
    void UpdateCameraButtons();

    // 설정
    void LoadConfigs();
    void SaveConfigs();
    CString GetIniPath() const;

    // <<< 상태 표시줄 관련 함수 추가 >>>
    void InitializeStatusBar();
    void UpdateStatusBarPane(int nPaneIndex, const CString& sText);
    // <<< --- 추가 끝 --- >>>


    DECLARE_MESSAGE_MAP()
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg LRESULT OnTcpStatus(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraDisconnected(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraStatus(WPARAM wParam, LPARAM lParam);

    // <<< 상태 표시줄 업데이트 핸들러 추가 >>>
    afx_msg LRESULT OnUpdateStatusPane(WPARAM wParam, LPARAM lParam);
    // <<< 타이머 핸들러 추가 >>>
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    // <<< --- 추가 끝 --- >>>

    afx_msg void OnCameraViewClick(UINT nID);
    afx_msg void OnManualCaptureClick();
    afx_msg void OnSettingsClick();

    // 패널 토글/도킹
    afx_msg void OnTogglePanel();
    afx_msg void OnDockLeft();
    afx_msg void OnDockRight();
    afx_msg void OnFloatPanel();
    afx_msg void OnHidePanel();
};
