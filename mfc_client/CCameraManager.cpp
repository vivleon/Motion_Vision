#include "pch.h"
#include "CCameraManager.h"
#include "MainFrm.h"
#include <string>
// #include <Pylon_TLParams.h> // 찾을 수 없음 - Pylon SDK 포함 경로 확인 필요
#include <pylon/PylonIncludes.h> // Pylon 기본 헤더 포함
#include <stdlib.h>
#include <process.h> // _beginthreadex

using namespace Pylon;
using namespace GenApi; // String_t 사용 위해 추가 

// Helper function to safely get NodeMap
INodeMap* GetSafeNodeMap(Pylon::CInstantCamera* pCam)
{
    if (!pCam || !pCam->IsOpen()) return nullptr;
    try {
        return &(pCam->GetNodeMap());
    }
    catch (const GenericException&) {
        // Log or handle error if needed
        return nullptr;
    }
}

CCameraManager::CCameraManager() {
    memset(m_hGrabThreads, 0, sizeof(m_hGrabThreads));
    memset(m_bThreadStopFlags, 0, sizeof(m_bThreadStopFlags));
    memset(m_bManualTrigger, 0, sizeof(m_bManualTrigger));
}
CCameraManager::~CCameraManager() { DisconnectAll(); }

void CCameraManager::FindDevices(std::vector<Pylon::CDeviceInfo>& out) {
    try {
        CTlFactory& f = CTlFactory::GetInstance();
        DeviceInfoList_t list;
        f.EnumerateDevices(list);
        out.clear();
        for (auto& d : list) out.push_back(d);
    }
    catch (const GenericException& e) {
        AfxMessageBox(CString(e.GetDescription()), MB_ICONERROR);
        out.clear();
    }
}

BOOL CCameraManager::ConnectCamera(const CameraConfig& cfg, HWND hNotifyWnd) {
    const int i = cfg.nIndex;
    if (i < 0 || i >= MAX_CAMERAS || m_Cameras[i].IsPylonDeviceAttached()) return FALSE;

    try {
        CDeviceInfo info;
        // --- Pylon::String_t으로 변환 ---
        // Pylon::String_t은 보통 std::string 또는 Pylon 내부 문자열 타입입니다.
        // CT2A는 const char*를 반환하므로 이를 String_t으로 변환합니다.
        info.SetSerialNumber(Pylon::String_t(CT2A(cfg.sSerial)));
        m_Cameras[i].Attach(CTlFactory::GetInstance().CreateDevice(info));
        m_Cameras[i].Open();

        // <<< 카메라 파라미터 설정 (Open 이후) >>>
        try
        {
            // 예시: Exposure Time 설정 (Float 노드)
            // 실제 카메라의 노드 이름 확인 필요 (예: "ExposureTime", "ExposureTimeAbs")
            SetParameterFloat(i, cfg.dExposureTime, "ExposureTimeAbs"); // 또는 "ExposureTime"

            // 예시: Gain 설정 (Float 노드)
            // 실제 카메라의 노드 이름 확인 필요 (예: "Gain", "GainRaw", "GainAbs")
            SetParameterFloat(i, cfg.dGain, "Gain"); // 또는 "GainAbs", "GainRaw"

            // 여기에 다른 파라미터 설정 추가 (예: PixelFormat, TriggerMode 등)
            // SetParameterEnum(i, _T("Mono8"), "PixelFormat"); // 예시
            // SetParameterEnum(i, _T("Off"), "TriggerMode");   // 예시

        }
        catch (const GenericException& e) {
            CString msg;
            msg.Format(_T("Camera %d parameter setting failed: %s"), i + 1, CString(e.GetDescription()));
            AfxMessageBox(msg, MB_ICONWARNING);
            // 연결을 계속 진행할지 여부 결정 (여기서는 계속 진행)
        }

        if (!m_Comms[i].Connect(cfg.sIp, cfg.nPort, hNotifyWnd, i)) {
            if (m_Cameras[i].IsOpen()) m_Cameras[i].Close();
            if (m_Cameras[i].IsPylonDeviceAttached()) m_Cameras[i].DetachDevice();
            return FALSE;
        }

        m_Detectors[i] = cv::createBackgroundSubtractorMOG2();
        UpdateMotionSettings(i, cfg.bMotionEnabled, cfg.nMotionThreshold); // 모션 설정 적용
        m_bManualTrigger[i] = FALSE;

        m_bThreadStopFlags[i] = FALSE;
        m_ThreadParams[i] = {}; // 구조체 초기화
        m_ThreadParams[i].nCameraIndex = i;
        m_ThreadParams[i].hNotifyWnd = hNotifyWnd;
        m_ThreadParams[i].pCamera = &m_Cameras[i];
        m_ThreadParams[i].pCommLock = m_Comms[i].GetLock();
        m_ThreadParams[i].pCommunicator = &m_Comms[i];
        // Detector 포인터 전달 수정 (get()으로 원시 포인터 얻기)
        m_ThreadParams[i].pDetector = (void*)m_Detectors[i].get();
        m_ThreadParams[i].config = cfg; // 설정 복사

        unsigned tid;
        m_hGrabThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, GrabThread, &m_ThreadParams[i], 0, &tid);
        if (!m_hGrabThreads[i]) { DisconnectCamera(i); return FALSE; }

        m_Cameras[i].StartGrabbing(GrabStrategy_LatestImageOnly);
    }
    catch (const GenericException& e) {
        DisconnectCamera(i);
        AfxMessageBox(CString(e.GetDescription()), MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}

void CCameraManager::DisconnectCamera(int i) {
    if (i < 0 || i >= MAX_CAMERAS) return;

    if (m_hGrabThreads[i]) {
        m_bThreadStopFlags[i] = TRUE;
        WaitForSingleObject(m_hGrabThreads[i], 3000);
        CloseHandle(m_hGrabThreads[i]); m_hGrabThreads[i] = nullptr;
    }
    try {
        if (m_Cameras[i].IsGrabbing()) m_Cameras[i].StopGrabbing();
        if (m_Cameras[i].IsOpen()) m_Cameras[i].Close();
        if (m_Cameras[i].IsPylonDeviceAttached()) m_Cameras[i].DetachDevice();
    }
    catch (...) {}
    if (m_Comms[i].IsConnected()) m_Comms[i].Disconnect();
    if (m_Detectors[i]) m_Detectors[i].release();
    m_bThreadStopFlags[i] = FALSE;
}
void CCameraManager::DisconnectAll() { for (int i = 0;i < MAX_CAMERAS;++i) DisconnectCamera(i); }

BOOL CCameraManager::IsCameraConnected(int i) {
    return (i >= 0 && i < MAX_CAMERAS) && m_Cameras[i].IsPylonDeviceAttached() && m_Comms[i].IsConnected() && m_hGrabThreads[i] != nullptr;
}
void CCameraManager::TriggerManualCapture(int i) { if (i >= 0 && i < MAX_CAMERAS) m_bManualTrigger[i] = TRUE; }

// UpdateMotionSettings 함수는 GrabThreadParams의 config를 직접 수정
void CCameraManager::UpdateMotionSettings(int i, BOOL en, int th) {
    if (i < 0 || i >= MAX_CAMERAS) return;
    // 스레드 파라미터가 유효한지 확인 후 접근
    // GrabThread는 이 config 값을 참조하므로, 스레드 동작 중에 변경 가능
    m_ThreadParams[i].config.bMotionEnabled = en;
    m_ThreadParams[i].config.nMotionThreshold = th;
}

// <<< GenICam 파라미터 접근 함수 구현 >>>
BOOL CCameraManager::GetParameterFloat(int nCamIndex, double& dValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CFloatPtr ptrFloat = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsReadable(ptrFloat)) return FALSE;
        dValue = ptrFloat->GetValue();
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::SetParameterFloat(int nCamIndex, double dValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CFloatPtr ptrFloat = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsWritable(ptrFloat)) return FALSE;

        // 경계 값 확인 및 조정 (선택 사항)
        double minVal = ptrFloat->GetMin();
        double maxVal = ptrFloat->GetMax();
        if (dValue < minVal) dValue = minVal;
        if (dValue > maxVal) dValue = maxVal;

        ptrFloat->SetValue(dValue);
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::GetParameterInt(int nCamIndex, int64_t& nValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CIntegerPtr ptrInt = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsReadable(ptrInt)) return FALSE;
        nValue = ptrInt->GetValue();
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::SetParameterInt(int nCamIndex, int64_t nValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CIntegerPtr ptrInt = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsWritable(ptrInt)) return FALSE;

        int64_t minVal = ptrInt->GetMin();
        int64_t maxVal = ptrInt->GetMax();
        if (nValue < minVal) nValue = minVal;
        if (nValue > maxVal) nValue = maxVal;

        ptrInt->SetValue(nValue);
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::GetParameterEnum(int nCamIndex, CString& sValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CEnumerationPtr ptrEnum = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsReadable(ptrEnum)) return FALSE;
        sValue = CString(ptrEnum->GetCurrentEntry()->GetSymbolic());
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::SetParameterEnum(int nCamIndex, const CString& sValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CEnumerationPtr ptrEnum = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsWritable(ptrEnum)) return FALSE;

        String_t valToSet(CT2A(sValue));
        IEnumEntry* pEntry = ptrEnum->GetEntryByName(valToSet);
        if (!pEntry || !IsReadable(pEntry)) // 해당 항목이 없거나 읽을 수 없으면 실패
        {
            // 사용 가능한 항목 목록을 얻어와서 비교해볼 수도 있음
            return FALSE;
        }

        ptrEnum->FromString(valToSet); // 또는 ptrEnum->SetIntValue(pEntry->GetValue());
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

BOOL CCameraManager::ExecuteCommand(int nCamIndex, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CCommandPtr ptrCommand = pNodeMap->GetNode(String_t(szNodeName));
        if (!IsWritable(ptrCommand)) return FALSE; // Execute는 Write 권한 필요
        ptrCommand->Execute();
        // 완료 대기 (필요한 경우)
        // while (!ptrCommand->IsDone()) { Sleep(10); }
        return TRUE;
    }
    catch (const GenericException&) { return FALSE; }
}

// Grab thread
UINT __cdecl CCameraManager::GrabThread(LPVOID p) {
    auto* prm = static_cast<GrabThreadParams*>(p);
    if (!prm) return 1;

    const int cam = prm->nCameraIndex;
    auto* pCam = prm->pCamera;
    auto* pComm = static_cast<CTcpCommunicator*>(prm->pCommunicator);
    auto* pDet = static_cast<cv::BackgroundSubtractorMOG2*>(prm->pDetector);
    auto* lk = prm->pCommLock;
    // GrabThreadParams에 있는 config를 사용 (복사본 생성 불필요)

    // MainFrame → manager 접근
    auto* frameWnd = dynamic_cast<CFrameWnd*>(AfxGetMainWnd());
    auto* mainFrm = dynamic_cast<class CMainFrame*>(frameWnd);
    // CCameraManager* mgr = mainFrm ? mainFrm->GetCameraManager() : nullptr;
     // 주의: GrabThread 내에서 Manager 객체(mgr)를 직접 사용하는 것은
     // 스레드 안전성 문제가 있을 수 있음. m_bThreadStopFlags 접근은 괜찮으나,
     // 다른 멤버 변수(특히 설정값) 직접 접근/수정은 피하고 prm 구조체를 통해야 함.
     // m_bThreadStopFlags는 CCameraManager 멤버이므로 mgr을 통해 접근해야 함.
     // -> CCameraManager* mgr = pCameraManager; // 생성자 등에서 Manager 포인터를 받아 저장하는 방식 고려
     // -> 여기서는 mainFrm을 통해 접근하는 기존 방식 유지하되, 주의 필요
    auto* mgr = mainFrm ? mainFrm->GetCameraManager() : nullptr;// 스레드 종료 플래그 접근용

    if (!pCam || !pComm || !pDet || !lk || !mgr) {
        if (prm->hNotifyWnd) ::PostMessage(prm->hNotifyWnd, WM_CAMERA_STATUS, cam, (LPARAM)new CString(_T("Thread init error")));
        return 2;
    }

    CPylonImage pimg; CGrabResultPtr grab; CImageFormatConverter cvt; cvt.OutputPixelFormat = PixelType_BGR8packed;

    while (!mgr->m_bThreadStopFlags[cam]) { // mgr을 통해 종료 플래그 접근
        try {
            if (!pCam->IsGrabbing()) // 안전장치: 혹시 Grabbing이 멈췄으면 루프 탈출
            {
                Sleep(100);
                continue;
            }

            // 타임아웃 3초 -> 1초로 줄여서 반응성 개선 (선택 사항)
            if (!pCam->RetrieveResult(1000, grab, TimeoutHandling_Return)) {
                // 타임아웃 발생, 연결 상태 등 확인 로직 추가 가능
                continue;
            }
            if (!grab.IsValid() || !grab->GrabSucceeded()) {
                // Grab 실패 처리 (예: 로그 남기기)
                if (grab.IsValid()) {
                    // Grab 실패 오류 코드/설명 로깅 가능
                   // CString err; err.Format(_T("Grab Failed Cam %d: Code %u, Desc: %s"), cam, grab->GetErrorCode(), CString(grab->GetErrorDescription()));
                   // Log(err);
                }
                continue;
            }

            cvt.Convert(pimg, grab);
            if (!pimg.IsValid()) continue; // 변환 실패 시 건너뛰기

            cv::Mat frm((int)pimg.GetHeight(), (int)pimg.GetWidth(), CV_8UC3, (uint8_t*)pimg.GetBuffer());
            if (frm.empty()) continue; // Mat 생성 실패 시 건너뛰기


            // 화면 송출 (PostMessage 방식 유지)
            if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                cv::Mat* copy = nullptr;
                try {
                    copy = new cv::Mat(frm.clone()); // 복제본 생성
                    // PostMessage 실패 시 메모리 해제
                    if (!::PostMessage(prm->hNotifyWnd, WM_UPDATE_FRAME, cam, (LPARAM)copy)) {
                        delete copy;
                        copy = nullptr;
                    }
                }
                catch (const cv::Exception& ex) {
                    // cv::Mat 복제 중 예외 처리
                    delete copy; // 실패 시 메모리 해제
                    // AfxTrace(_T("cv::Mat clone exception: %s\n"), CString(ex.what()));
                }
                catch (...) {
                    // 기타 예외 처리
                    delete copy;
                    // AfxTrace(_T("Unknown exception during cv::Mat clone or PostMessage.\n"));
                }
            }

            // 전송 조건 (prm->config 사용)
            BOOL send = FALSE;
            if (prm->config.bMotionEnabled) { // 모션 감지 활성화 시
                try {
                    cv::Mat mask;
                    // pDet 포인터 유효성 검사 추가
                    if (pDet) {
                        pDet->apply(frm, mask); // 배경 제거 적용
                        // Threshold 값과 비교하여 전송 여부 결정
                        if (!mask.empty() && cv::countNonZero(mask) > prm->config.nMotionThreshold) {
                            send = TRUE;
                        }
                    }
                }
                catch (const cv::Exception& ex) {
                    // cv::apply 등에서 예외 발생 시 처리
                    // AfxTrace(_T("Motion detection exception: %s\n"), CString(ex.what()));
                }
                catch (...) {
                    // AfxTrace(_T("Unknown exception during motion detection.\n"));
                }
            }

            // 수동 트리거 확인 (mgr을 통해 접근)
            if (mgr->m_bManualTrigger[cam]) {
                send = TRUE;
                mgr->m_bManualTrigger[cam] = FALSE; // 플래그 리셋
            }

            // 이미지 전송
            if (send && pComm && pComm->IsConnected()) { // pComm 유효성 검사 추가
                DWORD ts = GetTickCount(); // 타임스탬프
                std::vector<uchar> jpg;
                try {
                    // JPEG 인코딩 (품질 90)
                    if (cv::imencode(".jpg", frm, jpg, { cv::IMWRITE_JPEG_QUALITY, 90 }) && !jpg.empty()) {
                        // 통신 객체 잠금 후 전송
                        CSingleLock lock(lk, TRUE); // lk 유효성 검사는 위에서 함
                        if (lock.IsLocked()) {
                            pComm->SendImage(jpg.data(), (int)jpg.size(), ts);
                        }
                    }
                }
                catch (const cv::Exception& ex) {
                    // cv::imencode 예외 처리
                    // AfxTrace(_T("JPEG encoding exception: %s\n"), CString(ex.what()));
                }
                catch (...) {
                    // AfxTrace(_T("Unknown exception during JPEG encoding or sending.\n"));
                }
            }

            // 서버로부터 JSON 결과 수신 (pComm 유효성 검사 추가)
            if (pComm && pComm->IsConnected()) {
                nlohmann::json js;
                try {
                    // 결과 큐에서 하나씩 꺼내 처리
                    while (pComm->TryPopResult(js)) {
                        InspectionResult* r = new InspectionResult{}; // 결과 구조체 할당
                        // JSON 파싱 및 구조체 매핑
                        r->nCameraIndex = cam;
                        // value() 사용 시 기본값 지정 필수
                        r->sProduct = CString(js.value("product", "").c_str());
                        r->sFinalResult = CString(js.value("final", "").c_str());
                        r->sTime = CString(js.value("time", "").c_str());
                        r->sDefectSummary = CString(js.value("summary", "").c_str());
                        r->dwTimestamp = GetTickCount(); // 수신 시점 타임스탬프

                        // "items" 배열 처리
                        if (js.contains("items") && js["items"].is_array()) {
                            for (auto& it : js["items"]) {
                                if (!it.is_object()) continue; // 객체가 아니면 건너뜀
                                DefectItem di;
                                di.sCameraType = CString(it.value("cam", "").c_str());
                                di.sDefectType = CString(it.value("type", "").c_str());
                                di.sDefectValue = CString(it.value("value", "").c_str());
                                di.fConfidence = it.value("conf", 0.0f); // float 기본값
                                di.sAdditionalInfo = CString(it.value("info", "").c_str());
                                r->vecDefects.push_back(std::move(di)); // 이동 생성자 활용
                            }
                        }

                        // 메인 윈도우로 결과 전송 (PostMessage)
                        if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                            // PostMessage 실패 시 메모리 해제
                            if (!::PostMessage(prm->hNotifyWnd, WM_INSPECTION_RESULT, 0, (LPARAM)r)) {
                                delete r; // 중요: PostMessage 실패 시 new로 할당된 메모리 해제
                            }
                        }
                        else {
                            delete r; // 알림 대상 윈도우가 없으면 메모리 해제
                        }
                    } // end while TryPopResult
                }
                catch (const nlohmann::json::exception& e) {
                    // JSON 파싱 예외 처리
                    // AfxTrace(_T("JSON parsing exception: %s\n"), CString(e.what()));
                }
                catch (...) {
                    // 기타 예외 처리
                    // AfxTrace(_T("Unknown exception during result processing.\n"));
                }
            } // end if pComm Connected

        } // end try (main grab loop)
        catch (const GenericException& e) {
            // Pylon API 예외 처리 (예: RetrieveResult 등)
            // AfxTrace(_T("Pylon GenericException in GrabThread Cam %d: %s\n"), cam, CString(e.GetDescription()));
            // 필요 시 연결 끊김 처리 또는 재시도 로직 추가
            Sleep(100); // 잠시 대기 후 재시도
        }
        catch (const std::exception& e) {
            // 표준 라이브러리 예외 처리
            // AfxTrace(_T("std::exception in GrabThread Cam %d: %s\n"), cam, CString(e.what()));
            Sleep(100);
        }
        catch (...) {
            // 알 수 없는 예외 처리
            // AfxTrace(_T("Unknown exception in GrabThread Cam %d.\n"), cam);
            // 루프를 계속할지, 종료할지 결정 필요
            Sleep(1000); // 더 길게 대기
        }
    } // end while (!stop flag)

   // 스레드 종료 전 정리 (필요한 경우)
    return 0; // 정상 종료
}