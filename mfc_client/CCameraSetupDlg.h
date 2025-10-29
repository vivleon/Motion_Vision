#pragma once
#include "afxdialogex.h"
#include <vector> // std::vector 사용 위해 추가
#include "SharedData.h" // CameraConfig 사용 위해 추가
#include <afxcmn.h> // CListCtrl 사용 위해 추가

class CCameraSetupDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CCameraSetupDlg)

public:
    CCameraSetupDlg(CWnd* pParent = nullptr);   // 표준 생성자입니다.
    virtual ~CCameraSetupDlg() = default; // 가상 소멸자 추가

    // 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CAMERA_SETUP }; // IDD_CAMERA_SETUP 확인 필요
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.
    virtual BOOL OnInitDialog(); // OnInitDialog 선언 추가

    CListCtrl m_list;
    void LoadAndDisplay();
    BOOL LoadFromIni(std::vector<CameraConfig>& out);
    BOOL SaveToIni(const std::vector<CameraConfig>& cfgs);

    // <<< 상세 설정 UI 컨트롤 멤버 변수 추가 (예시) >>>
    // 리소스 편집기에서 해당 ID로 Edit Control 등을 추가해야 함
    CEdit m_editExposure;
    CEdit m_editGain;
    // <<< --- 추가 끝 --- >>>

    // <<< 리스트 항목 선택 변경 핸들러 추가 >>>
    afx_msg void OnLvnItemchangedList1(NMHDR* pNMHDR, LRESULT* pResult);
    // <<< --- 추가 끝 --- >>>

    afx_msg void OnBnClickedSearch();
    afx_msg void OnBnClickedSave();

    DECLARE_MESSAGE_MAP()

private:
    // <<< 현재 선택된 리스트 인덱스 저장 변수 >>>
    int m_nCurrentSelection = -1;
    // <<< 설정값 임시 저장용 벡터 (UI와 동기화) >>>
    std::vector<CameraConfig> m_CurrentConfigs;
    // <<< 상세 설정 UI 업데이트 함수 >>>
    void UpdateDetailsUI(int nListIndex);
    // <<< 상세 설정 UI 값 저장 함수 >>>
    void SaveDetailsFromUI(int nListIndex);

};