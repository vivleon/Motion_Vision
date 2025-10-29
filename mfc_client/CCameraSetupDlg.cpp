#include "pch.h"
#include "CCameraSetupDlg.h"
#include "resource.h"
#include "CCameraManager.h"
#include "MainFrm.h"
#include <vector>
#include <string>
#include <array>
#include <pylon/DeviceInfo.h>

// [1] CCameraSetupDlg에 대한 런타임 클래스 구현 (CCameraSetupDlg::GetRuntimeClass 해결)
IMPLEMENT_DYNAMIC(CCameraSetupDlg, CDialogEx)

// --- 사용자 확인 필요: Resource.h 에 IDC_BUTTON_SEARCH, IDC_LIST1 정의 확인 ---

BEGIN_MESSAGE_MAP(CCameraSetupDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_SEARCH, &CCameraSetupDlg::OnBnClickedSearch)
    ON_BN_CLICKED(IDOK, &CCameraSetupDlg::OnBnClickedSave)
    // <<< 리스트 항목 변경 메시지 핸들러 추가 >>>
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CCameraSetupDlg::OnLvnItemchangedList1)
    // <<< 상세 설정 Edit Control 변경 핸들러 추가 (선택 사항) >>>
    // ON_EN_CHANGE(IDC_EDIT_EXPOSURE, &CCameraSetupDlg::OnEnChangeExposure) // 필요 시 구현
    // ON_EN_CHANGE(IDC_EDIT_GAIN, &CCameraSetupDlg::OnEnChangeGain)       // 필요 시 구현
END_MESSAGE_MAP()

CCameraSetupDlg::CCameraSetupDlg(CWnd* p) : CDialogEx(IDD_CAMERA_SETUP, p), m_nCurrentSelection(-1) {} // m_nCurrentSelection 초기화

void CCameraSetupDlg::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list);
    // <<< 상세 설정 컨트롤 연결 >>>
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure); // 리소스 ID 확인
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);       // 리소스 ID 확인
}

BOOL CCameraSetupDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES /*| LVS_EX_CHECKBOXES*/); // 체크박스 스타일 제거 (ON/OFF 텍스트 사용)
    m_list.InsertColumn(0, _T("Index"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(1, _T("Serial"), LVCFMT_LEFT, 150);
    m_list.InsertColumn(2, _T("Name"), LVCFMT_LEFT, 100); // <<< 이름(Name) 컬럼 추가 >>>
    m_list.InsertColumn(3, _T("IP"), LVCFMT_LEFT, 120);
    m_list.InsertColumn(4, _T("Port"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(5, _T("Motion"), LVCFMT_LEFT, 60); // <<< 컬럼 인덱스 변경됨 >>>
    m_list.InsertColumn(6, _T("Threshold"), LVCFMT_LEFT, 80); // <<< 컬럼 인덱스 변경됨 >>>

    LoadAndDisplay();
    UpdateDetailsUI(-1); // 초기에는 상세 설정 비활성화

    return TRUE;
}
// ini 로딩 후 리스트 및 내부 데이터 업데이트
void CCameraSetupDlg::LoadAndDisplay() {
    LoadFromIni(m_CurrentConfigs); // 멤버 변수 m_CurrentConfigs에 로드
    m_list.DeleteAllItems();
    for (const auto& c : m_CurrentConfigs) {
        CString strIndex;
        strIndex.Format(_T("%d"), c.nIndex + 1);
        int row = m_list.InsertItem(m_list.GetItemCount(), strIndex);
        m_list.SetItemText(row, 1, c.sSerial);
        m_list.SetItemText(row, 2, c.sFriendlyName); // 이름 표시
        m_list.SetItemText(row, 3, c.sIp);           // 컬럼 인덱스 +1
        CString strPort;
        strPort.Format(_T("%d"), c.nPort);
        m_list.SetItemText(row, 4, strPort);         // 컬럼 인덱스 +1
        m_list.SetItemText(row, 5, c.bMotionEnabled ? _T("ON") : _T("OFF")); // 컬럼 인덱스 +1
        CString strThreshold;
        strThreshold.Format(_T("%d"), c.nMotionThreshold);
        m_list.SetItemText(row, 6, strThreshold);    // 컬럼 인덱스 +1

        // <<< 각 행(Row)의 데이터 포인터로 해당 config 인덱스 저장 >>>
        m_list.SetItemData(row, (DWORD_PTR)c.nIndex);
    }
    m_nCurrentSelection = -1; // 선택 초기화
    UpdateDetailsUI(-1); // 상세 UI 업데이트
}

// 검색 버튼 클릭 시
void CCameraSetupDlg::OnBnClickedSearch() {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    auto* mgr = mf ? mf->GetCameraManager() : nullptr;
    if (!mgr) { AfxMessageBox(_T("Camera manager not ready.")); return; }

    std::vector<Pylon::CDeviceInfo> devs;
    mgr->FindDevices(devs);

    // 현재 UI/m_CurrentConfigs에 있는 시리얼 목록 만들기
    std::vector<CString> currentSerials;
    for (const auto& cfg : m_CurrentConfigs) {
        currentSerials.push_back(cfg.sSerial);
    }

    // 사용 중인 인덱스 추적
    std::array<bool, MAX_CAMERAS> used{};
    for (const auto& c : m_CurrentConfigs) {
        if (c.nIndex >= 0 && c.nIndex < MAX_CAMERAS)
            used[c.nIndex] = true;
    }

    bool bAdded = false;
    for (const auto& dv : devs) {
        CString serial(dv.GetSerialNumber());
        bool exists = false;
        for (const auto& s : currentSerials) {
            if (s == serial) { exists = true; break; }
        }
        if (exists) continue; // 이미 목록에 있으면 건너뜀

        // 사용 가능한 첫 번째 빈 슬롯(인덱스) 찾기
        int slot = -1;
        for (int i = 0; i < MAX_CAMERAS; ++i) {
            if (!used[i]) { slot = i; break; }
        }

        if (slot != -1) { // 빈 슬롯 찾음
            CameraConfig nc{};
            nc.nIndex = slot;
            nc.sSerial = serial;
            // 기본값 설정
            nc.sFriendlyName.Format(_T("CAM %d"), slot + 1); // 기본 이름
            nc.sIp = _T("127.0.0.1");
            nc.nPort = 9000 + slot; // 기본 포트 (9000번대)
            nc.bMotionEnabled = FALSE; // 기본 Motion OFF
            nc.nMotionThreshold = 5000; // 기본 Threshold
            // 상세 설정 기본값 (SharedData.h 기본값과 동일하게)
            nc.dExposureTime = 10000.0;
            nc.dGain = 1.0;

            m_CurrentConfigs.push_back(nc); // 내부 데이터에 추가
            used[slot] = true; // 슬롯 사용됨으로 표시
            bAdded = true;
        }
        else {
            AfxMessageBox(_T("더 이상 카메라를 추가할 수 없습니다 (최대 ") + std::to_wstring(MAX_CAMERAS).c_str() + _T("개)."), MB_ICONWARNING);
            break; // 슬롯 없으면 검색 중단
        }
    }

    if (bAdded) {
        // 리스트 컨트롤 새로 고침 (LoadAndDisplay 로직 재사용)
        m_list.DeleteAllItems();
        for (const auto& c : m_CurrentConfigs) {
            CString strIndex;
            strIndex.Format(_T("%d"), c.nIndex + 1);
            int row = m_list.InsertItem(m_list.GetItemCount(), strIndex);
            m_list.SetItemText(row, 1, c.sSerial);
            m_list.SetItemText(row, 2, c.sFriendlyName); // 이름 표시
            m_list.SetItemText(row, 3, c.sIp);           // 컬럼 인덱스 +1
            CString strPort;
            strPort.Format(_T("%d"), c.nPort);
            m_list.SetItemText(row, 4, strPort);         // 컬럼 인덱스 +1
            m_list.SetItemText(row, 5, c.bMotionEnabled ? _T("ON") : _T("OFF")); // 컬럼 인덱스 +1
            CString strThreshold;
            strThreshold.Format(_T("%d"), c.nMotionThreshold);
            m_list.SetItemText(row, 6, strThreshold);    // 컬럼 인덱스 +1
            m_list.SetItemData(row, (DWORD_PTR)c.nIndex); // 데이터 설정
        }
        m_nCurrentSelection = -1; // 선택 초기화
        UpdateDetailsUI(-1); // 상세 UI 업데이트
    }
    else {
        AfxMessageBox(_T("새로운 카메라를 찾지 못했거나 이미 목록에 있습니다."));
    }
}

// 저장 버튼 클릭 시
void CCameraSetupDlg::OnBnClickedSave() {
    // 현재 선택된 항목의 상세 UI 값을 내부 데이터(m_CurrentConfigs)에 저장
    if (m_nCurrentSelection >= 0 && m_nCurrentSelection < m_list.GetItemCount()) {
        SaveDetailsFromUI(m_nCurrentSelection);
    }

    // m_CurrentConfigs 벡터에 있는 모든 설정을 ini 파일에 저장
    if (SaveToIni(m_CurrentConfigs)) {
        CDialogEx::OnOK(); // 저장 성공 시 다이얼로그 닫기
    }
    else {
        AfxMessageBox(_T("설정 저장에 실패했습니다."), MB_ICONERROR);
        // 실패 시 다이얼로그 유지
    }
}

// ini 로딩 (MainFrame 멤버 함수 호출)
BOOL CCameraSetupDlg::LoadFromIni(std::vector<CameraConfig>& out) {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    return mf ? mf->LoadCameraConfigs(out) : FALSE;
}
// ini 저장 (MainFrame 멤버 함수 호출)
BOOL CCameraSetupDlg::SaveToIni(const std::vector<CameraConfig>& cfgs) {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    return mf ? mf->SaveCameraConfigs(cfgs) : FALSE;
}

// 리스트 컨트롤에서 선택된 항목이 변경될 때 호출
void CCameraSetupDlg::OnLvnItemchangedList1(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    *pResult = 0;

    // 선택 상태가 변경된 경우에만 처리
    if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uNewState & LVIS_SELECTED))
    {
        // 이전에 선택된 항목이 있었다면, UI의 변경 내용을 저장
        if (m_nCurrentSelection >= 0 && m_nCurrentSelection < m_list.GetItemCount())
        {
            SaveDetailsFromUI(m_nCurrentSelection);
        }

        // 새로 선택된 항목 인덱스 업데이트
        m_nCurrentSelection = pNMLV->iItem;

        // 상세 설정 UI 업데이트
        UpdateDetailsUI(m_nCurrentSelection);
    }
    else if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uOldState & LVIS_SELECTED) && !(pNMLV->uNewState & LVIS_SELECTED))
    {
        // 선택이 해제될 때 (다른 항목 클릭 등으로) 이전 항목 저장
        // 주의: 항목이 아예 없어지거나 리스트가 비워질 때는 iItem이 유효하지 않을 수 있음
        if (pNMLV->iItem >= 0 && pNMLV->iItem < m_list.GetItemCount())
        {
            // SaveDetailsFromUI(pNMLV->iItem); // 선택 해제 시 저장할 필요 없음 (새 항목 선택 시 저장됨)
        }

        // 현재 선택 없음으로 표시 (선택 사항)
        if (m_list.GetFirstSelectedItemPosition() == NULL) {
            m_nCurrentSelection = -1;
            UpdateDetailsUI(-1); // 선택 없으면 UI 비활성화
        }
    }
}

// 선택된 리스트 인덱스에 해당하는 카메라 설정을 상세 UI에 표시
void CCameraSetupDlg::UpdateDetailsUI(int nListIndex)
{
    if (nListIndex < 0 || nListIndex >= m_CurrentConfigs.size())
    {
        // 선택된 항목이 없거나 유효하지 않으면 UI 비활성화 및 내용 클리어
        m_editExposure.SetWindowText(_T(""));
        m_editGain.SetWindowText(_T(""));
        m_editExposure.EnableWindow(FALSE);
        m_editGain.EnableWindow(FALSE);
        return;
    }

    // 리스트 인덱스로부터 실제 카메라 설정 인덱스(nIndex) 찾기 (ItemData 사용)
    DWORD_PTR itemData = m_list.GetItemData(nListIndex);
    int configIndex = -1;
    for (size_t i = 0; i < m_CurrentConfigs.size(); ++i) {
        if (m_CurrentConfigs[i].nIndex == (int)itemData) {
            configIndex = (int)i;
            break;
        }
    }

    if (configIndex < 0) { // 해당하는 설정을 찾지 못함 (오류 상황)
        UpdateDetailsUI(-1); // 비활성화 처리
        return;
    }


    const CameraConfig& cfg = m_CurrentConfigs[configIndex];

    // UI 활성화
    m_editExposure.EnableWindow(TRUE);
    m_editGain.EnableWindow(TRUE);

    // 값 표시 (Format 사용)
    CString val;
    val.Format(_T("%.1f"), cfg.dExposureTime);
    m_editExposure.SetWindowText(val);

    val.Format(_T("%.2f"), cfg.dGain);
    m_editGain.SetWindowText(val);
}

// 상세 UI 컨트롤의 값을 현재 선택된 카메라 설정(m_CurrentConfigs)에 저장
void CCameraSetupDlg::SaveDetailsFromUI(int nListIndex)
{
    if (nListIndex < 0 || nListIndex >= m_list.GetItemCount()) return; // 유효한 선택 아님

    // 리스트 인덱스로부터 실제 카메라 설정 인덱스(nIndex) 찾기 (ItemData 사용)
    DWORD_PTR itemData = m_list.GetItemData(nListIndex);
    int configIndex = -1;
    for (size_t i = 0; i < m_CurrentConfigs.size(); ++i) {
        if (m_CurrentConfigs[i].nIndex == (int)itemData) {
            configIndex = (int)i;
            break;
        }
    }

    if (configIndex < 0) return; // 해당하는 설정을 찾지 못함 (오류 상황)

    CameraConfig& cfg = m_CurrentConfigs[configIndex]; // 참조로 가져와 직접 수정

    CString val;

    // Exposure 값 읽기 및 변환
    m_editExposure.GetWindowText(val);
    cfg.dExposureTime = _ttof(val); // 문자열을 double로 변환

    // Gain 값 읽기 및 변환
    m_editGain.GetWindowText(val);
    cfg.dGain = _ttof(val); // 문자열을 double로 변환

    // TODO: 다른 상세 설정 값들도 여기서 저장
}

/* // 선택 사항: Edit Control 값이 변경될 때마다 m_CurrentConfigs 업데이트 (실시간 반영)
void CCameraSetupDlg::OnEnChangeExposure()
{
    if (m_nCurrentSelection >= 0) {
        SaveDetailsFromUI(m_nCurrentSelection);
    }
}

void CCameraSetupDlg::OnEnChangeGain()
{
     if (m_nCurrentSelection >= 0) {
        SaveDetailsFromUI(m_nCurrentSelection);
    }
}
*/