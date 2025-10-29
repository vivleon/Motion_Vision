#include "pch.h"
#include "CCameraSetupDlg.h"
#include "resource.h"
#include "CCameraManager.h" // CameraManager 접근 위해 필요
#include "MainFrm.h"        // MainFrame 접근 위해 필요
#include <vector>
#include <string>
#include <array>
#include <pylon/DeviceInfo.h> // Pylon 검색 결과 사용

IMPLEMENT_DYNAMIC(CCameraSetupDlg, CDialogEx)

// --- 리소스 ID 확인 필요 ---
// IDC_LIST1: 카메라 목록 리스트 컨트롤 ID
// IDC_EDIT_EXPOSURE: 노출 시간 Edit Control ID (새로 추가)
// IDC_EDIT_GAIN: 게인 Edit Control ID (새로 추가)
// IDC_BUTTON_SEARCH: 검색 버튼 ID
// IDOK: 저장 버튼 ID (기본값)
// --- --- --- --- --- --- ---

BEGIN_MESSAGE_MAP(CCameraSetupDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_SEARCH, &CCameraSetupDlg::OnBnClickedSearch)
    ON_BN_CLICKED(IDOK, &CCameraSetupDlg::OnBnClickedSave)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CCameraSetupDlg::OnLvnItemchangedList1)
    // <<< 상세 설정 Edit Control 변경 핸들러 메시지 맵 추가 >>>
    ON_EN_CHANGE(IDC_EDIT_EXPOSURE, &CCameraSetupDlg::OnEnChangeExposure) // ID 확인
    ON_EN_CHANGE(IDC_EDIT_GAIN, &CCameraSetupDlg::OnEnChangeGain)       // ID 확인
    // <<< --- 추가 끝 --- >>>
END_MESSAGE_MAP()

CCameraSetupDlg::CCameraSetupDlg(CWnd* p) : CDialogEx(IDD_CAMERA_SETUP, p), m_nCurrentSelection(-1) {} // m_nCurrentSelection 초기화

void CCameraSetupDlg::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list);
    // 상세 설정 컨트롤 연결
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure); // ID 확인
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);       // ID 확인
}

BOOL CCameraSetupDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    // 리스트 컨트롤 스타일 및 컬럼 설정
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES); // 체크박스 제거됨
    m_list.InsertColumn(0, _T("Index"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(1, _T("Serial"), LVCFMT_LEFT, 150);
    m_list.InsertColumn(2, _T("Name"), LVCFMT_LEFT, 100); // 이름 컬럼
    m_list.InsertColumn(3, _T("IP"), LVCFMT_LEFT, 120);
    m_list.InsertColumn(4, _T("Port"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(5, _T("Motion"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(6, _T("Threshold"), LVCFMT_LEFT, 80);

    LoadAndDisplay(); // 설정 로드 및 리스트 채우기
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
        m_list.SetItemText(row, 3, c.sIp);
        CString strPort;
        strPort.Format(_T("%d"), c.nPort);
        m_list.SetItemText(row, 4, strPort);
        // Motion 컬럼 인덱스 수정: 5
        m_list.SetItemText(row, 5, c.bMotionEnabled ? _T("ON") : _T("OFF"));
        CString strThreshold;
        strThreshold.Format(_T("%d"), c.nMotionThreshold);
        // Threshold 컬럼 인덱스 수정: 6
        m_list.SetItemText(row, 6, strThreshold);

        // 각 행(Row)의 데이터로 실제 설정의 인덱스(CameraConfig::nIndex) 저장
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

    std::vector<CString> currentSerials;
    for (const auto& cfg : m_CurrentConfigs) {
        currentSerials.push_back(cfg.sSerial);
    }

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
        if (exists) continue;

        int slot = -1;
        for (int i = 0; i < MAX_CAMERAS; ++i) {
            if (!used[i]) { slot = i; break; }
        }

        if (slot != -1) {
            CameraConfig nc{};
            nc.nIndex = slot;
            nc.sSerial = serial;
            nc.sFriendlyName.Format(_T("CAM %d"), slot + 1);
            nc.sIp = _T("127.0.0.1");
            nc.nPort = 9000 + slot; // 기본 포트 9000번대
            nc.bMotionEnabled = FALSE; // 기본 Motion OFF
            nc.nMotionThreshold = 5000; // 기본 Threshold 5000
            nc.dExposureTime = 10000.0; // 기본 Exposure
            nc.dGain = 1.0;            // 기본 Gain

            m_CurrentConfigs.push_back(nc);
            used[slot] = true;
            bAdded = true;
        }
        else {
            CString msg;
            msg.Format(_T("더 이상 카메라를 추가할 수 없습니다 (최대 %d개)."), MAX_CAMERAS);
            AfxMessageBox(msg, MB_ICONWARNING);
            break;
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
            m_list.SetItemText(row, 2, c.sFriendlyName);
            m_list.SetItemText(row, 3, c.sIp);
            CString strPort;
            strPort.Format(_T("%d"), c.nPort);
            m_list.SetItemText(row, 4, strPort);
            m_list.SetItemText(row, 5, c.bMotionEnabled ? _T("ON") : _T("OFF")); // 컬럼 5
            CString strThreshold;
            strThreshold.Format(_T("%d"), c.nMotionThreshold);
            m_list.SetItemText(row, 6, strThreshold); // 컬럼 6
            m_list.SetItemData(row, (DWORD_PTR)c.nIndex); // ItemData 설정
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
    // 현재 선택된 항목의 상세 UI 값을 내부 데이터(m_CurrentConfigs)에 먼저 저장
    if (m_nCurrentSelection >= 0) {
        SaveDetailsFromUI(m_nCurrentSelection);
    }

    // m_CurrentConfigs 벡터 전체를 ini 파일에 저장
    if (SaveToIni(m_CurrentConfigs)) {
        CDialogEx::OnOK(); // 저장 성공 시 다이얼로그 닫기
    }
    else {
        AfxMessageBox(_T("설정 저장에 실패했습니다."), MB_ICONERROR);
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

    // 선택 상태가 변경된 아이템에 대해서만 처리
    if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uNewState & LVIS_SELECTED))
    {
        // 이전에 선택된 항목이 있었다면, UI의 변경 내용을 m_CurrentConfigs에 저장
        // (주의: OnEnChange 핸들러에서도 저장을 하므로 중복될 수 있으나,
        //  리스트 클릭 시 확실히 저장하기 위해 여기서도 호출하는 것이 안전할 수 있음)
        if (m_nCurrentSelection >= 0)
        {
            SaveDetailsFromUI(m_nCurrentSelection);
        }

        // 새로 선택된 항목 인덱스 업데이트 (리스트 컨트롤 상의 인덱스)
        m_nCurrentSelection = pNMLV->iItem;

        // 해당 항목의 상세 설정을 UI에 표시
        UpdateDetailsUI(m_nCurrentSelection);
    }
    // 선택이 해제될 때 (리스트의 다른 곳을 클릭하거나 포커스를 잃는 등)
    else if ((pNMLV->uChanged & LVIF_STATE) && (pNMLV->uOldState & LVIS_SELECTED) && !(pNMLV->uNewState & LVIS_SELECTED))
    {
        // 만약 현재 선택된 항목이 없고, 해제된 항목이 이전에 선택했던 항목이라면
        // 현재 선택 없음을 표시
        if (m_list.GetFirstSelectedItemPosition() == NULL && pNMLV->iItem == m_nCurrentSelection) {
            // 변경 내용 저장 (선택 해제 직전 상태 저장)
            SaveDetailsFromUI(m_nCurrentSelection);

            m_nCurrentSelection = -1; // 선택 없음
            UpdateDetailsUI(-1);      // 상세 UI 비활성화
        }
        // 다른 항목이 선택되면서 기존 항목이 해제되는 경우는 위의 if (LVIS_SELECTED) 블록에서 처리됨
    }
}

// 리스트 인덱스(화면상 순서)를 받아서 m_CurrentConfigs 벡터 내의 인덱스를 반환
// 못 찾으면 -1 반환
int CCameraSetupDlg::FindConfigIndexFromListIndex(int nListIndex)
{
    if (nListIndex < 0 || nListIndex >= m_list.GetItemCount()) return -1;

    // 리스트 아이템의 데이터(저장된 CameraConfig::nIndex)를 가져옴
    DWORD_PTR itemData = m_list.GetItemData(nListIndex);
    int targetConfigNIndex = (int)itemData;

    // m_CurrentConfigs 벡터에서 해당 nIndex를 가진 요소를 찾음
    for (size_t i = 0; i < m_CurrentConfigs.size(); ++i) {
        if (m_CurrentConfigs[i].nIndex == targetConfigNIndex) {
            return (int)i; // 벡터 내 인덱스 반환
        }
    }
    return -1; // 못 찾음
}


// 선택된 리스트 인덱스에 해당하는 카메라 설정을 상세 UI에 표시
void CCameraSetupDlg::UpdateDetailsUI(int nListIndex)
{
    // 리스트 인덱스로부터 실제 설정 데이터 인덱스 찾기
    int configIndex = FindConfigIndexFromListIndex(nListIndex);

    if (configIndex < 0) // 유효한 선택이 아니거나 데이터 못 찾음
    {
        // UI 비활성화 및 내용 클리어
        m_editExposure.SetWindowText(_T(""));
        m_editGain.SetWindowText(_T(""));
        m_editExposure.EnableWindow(FALSE);
        m_editGain.EnableWindow(FALSE);
        // 다른 상세 UI 컨트롤들도 비활성화...
        return;
    }

    // 해당 설정 데이터 가져오기
    const CameraConfig& cfg = m_CurrentConfigs[configIndex];

    // UI 활성화
    m_editExposure.EnableWindow(TRUE);
    m_editGain.EnableWindow(TRUE);
    // 다른 상세 UI 컨트롤들도 활성화...

    // 값 표시 (Format 사용)
    CString val;
    val.Format(_T("%.1f"), cfg.dExposureTime);
    m_editExposure.SetWindowText(val);

    val.Format(_T("%.2f"), cfg.dGain);
    m_editGain.SetWindowText(val);

    // 다른 상세 UI 값들도 표시...
}

// 상세 UI 컨트롤의 값을 현재 선택된 카메라 설정(m_CurrentConfigs)에 저장
void CCameraSetupDlg::SaveDetailsFromUI(int nListIndex)
{
    // 리스트 인덱스로부터 실제 설정 데이터 인덱스 찾기
    int configIndex = FindConfigIndexFromListIndex(nListIndex);

    if (configIndex < 0) return; // 유효한 선택 아님 또는 데이터 못 찾음

    // 해당 설정 데이터에 대한 참조 얻기
    CameraConfig& cfg = m_CurrentConfigs[configIndex];

    CString val;

    // Exposure 값 읽기 및 변환/저장
    m_editExposure.GetWindowText(val);
    cfg.dExposureTime = _ttof(val); // 문자열을 double로 변환, 실패 시 0.0 반환

    // Gain 값 읽기 및 변환/저장
    m_editGain.GetWindowText(val);
    cfg.dGain = _ttof(val); // 문자열을 double로 변환, 실패 시 0.0 반환

    // 다른 상세 설정 값들도 UI에서 읽어와서 cfg에 저장...
    // 예: m_comboPixelFormat.GetWindowText(val); cfg.sPixelFormat = val;
}

// <<< 상세 설정 Edit Control 변경 핸들러 구현 >>>
void CCameraSetupDlg::OnEnChangeExposure()
{
    // 현재 리스트에 선택된 항목이 있을 때만 내부 데이터 업데이트
    if (m_nCurrentSelection >= 0) {
        SaveDetailsFromUI(m_nCurrentSelection);
        // 실시간 적용이 필요하다면 여기서 CCameraManager의 함수 호출
        // int configIndex = FindConfigIndexFromListIndex(m_nCurrentSelection);
        // if (configIndex >= 0) {
        //     auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
        //     if (mf) {
        //         mf->GetCameraManager()->SetParameterFloat(
        //             m_CurrentConfigs[configIndex].nIndex, // 실제 카메라 nIndex 사용
        //             m_CurrentConfigs[configIndex].dExposureTime,
        //             "ExposureTimeAbs"); // 실제 노드 이름 사용
        //     }
        // }
    }
}

void CCameraSetupDlg::OnEnChangeGain()
{
    // 현재 리스트에 선택된 항목이 있을 때만 내부 데이터 업데이트
    if (m_nCurrentSelection >= 0) {
        SaveDetailsFromUI(m_nCurrentSelection);
        // 실시간 적용이 필요하다면 여기서 CCameraManager의 함수 호출
       // int configIndex = FindConfigIndexFromListIndex(m_nCurrentSelection);
       // if (configIndex >= 0) {
       //     auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
       //     if (mf) {
       //         mf->GetCameraManager()->SetParameterFloat(
       //             m_CurrentConfigs[configIndex].nIndex,
       //             m_CurrentConfigs[configIndex].dGain,
       //             "Gain"); // 실제 노드 이름 사용
       //     }
       // }
    }
}
// <<< --- 구현 끝 --- >>>