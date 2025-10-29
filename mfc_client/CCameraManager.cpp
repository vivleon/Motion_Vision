#include "pch.h"
#include "CCameraManager.h"
#include "MainFrm.h" // AfxGetMainWnd() 사용 가능성
#include <string>
#include <pylon/PylonIncludes.h>
#include <process.h> // _beginthreadex
#include <atlconv.h> // CT2A, USES_CONVERSION 사용 위해 필요

using namespace Pylon;
using namespace GenApi;

// 안전하게 NodeMap 얻는 헬퍼 함수 (변경 없음)
INodeMap* GetSafeNodeMap(Pylon::CInstantCamera* pCam)
{
    if (!pCam || !pCam->IsOpen()) return nullptr;
    try {
        return &(pCam->GetNodeMap());
    }
    catch (const GenericException& e) {
        TRACE(_T("Error getting NodeMap: %s\n"), CString(e.GetDescription()));
        return nullptr;
    }
}


CCameraManager::CCameraManager() {
    memset(m_hGrabThreads, 0, sizeof(m_hGrabThreads));
    memset(m_bThreadStopFlags, 0, sizeof(m_bThreadStopFlags));
    memset(m_bManualTrigger, 0, sizeof(m_bManualTrigger));
}

CCameraManager::~CCameraManager() {
    DisconnectAll();
}

void CCameraManager::FindDevices(std::vector<Pylon::CDeviceInfo>& out) {
    out.clear();
    try {
        CTlFactory& f = CTlFactory::GetInstance();
        DeviceInfoList_t list;
        int found = f.EnumerateDevices(list);
        TRACE(_T("Found %d Pylon devices.\n"), found);
        for (size_t i = 0; i < list.size(); ++i) {
            out.push_back(list[i]);
        }
    }
    catch (const GenericException& e) {
        CString msg;
        msg.Format(_T("Pylon EnumerateDevices Error: %s"), CString(e.GetDescription()));
        AfxMessageBox(msg, MB_ICONERROR);
        out.clear();
    }
}

BOOL CCameraManager::ConnectCamera(const CameraConfig& cfg, HWND hNotifyWnd) {
    const int i = cfg.nIndex;
    if (i < 0 || i >= MAX_CAMERAS) {
        TRACE(_T("ConnectCamera failed: Invalid index %d.\n"), i);
        return FALSE;
    }
    if (m_Cameras[i].IsPylonDeviceAttached()) {
        TRACE(_T("ConnectCamera failed: Camera %d already attached.\n"), i);
        return FALSE;
    }

    TRACE(_T("Attempting to connect camera %d (Serial: %s)...\n"), i, cfg.sSerial);

    try {
        CTlFactory& factory = CTlFactory::GetInstance();
        CDeviceInfo info;
        USES_CONVERSION;
        info.SetSerialNumber(Pylon::String_t(T2A(cfg.sSerial)));

        m_Cameras[i].Attach(factory.CreateDevice(info));
        TRACE(_T("Camera %d attached.\n"), i);

        m_Cameras[i].Open();
        TRACE(_T("Camera %d opened.\n"), i);

        // --- Set Camera Parameters ---
        try
        {
            TRACE(_T("Setting parameters for camera %d...\n"), i);
            if (!SetParameterFloat(i, cfg.dExposureTime, "ExposureTimeAbs")) {
                TRACE(_T("Warning: Failed to set ExposureTimeAbs for camera %d.\n"), i);
            }
            else { TRACE(_T(" -> ExposureTimeAbs set to %.1f\n"), cfg.dExposureTime); }

            if (!SetParameterFloat(i, cfg.dGain, "Gain")) {
                TRACE(_T("Warning: Failed to set Gain for camera %d.\n"), i);
            }
            else { TRACE(_T(" -> Gain set to %.2f\n"), cfg.dGain); }
            // --- Add other parameter settings here ---

        }
        catch (const GenericException& e) {
            CString msg;
            msg.Format(_T("Camera %d parameter setting failed during connect: %s"), i + 1, CString(e.GetDescription()));
            AfxMessageBox(msg, MB_ICONWARNING);
        }
        // --- End Parameter Setting ---

        TRACE(_T("Connecting TCP communicator for camera %d to %s:%d...\n"), i, cfg.sIp, cfg.nPort);
        if (!m_Comms[i].Connect(cfg.sIp, cfg.nPort, hNotifyWnd, i)) {
            TRACE(_T("TCP Connect failed for camera %d.\n"), i);
            if (m_Cameras[i].IsOpen()) m_Cameras[i].Close();
            if (m_Cameras[i].IsPylonDeviceAttached()) m_Cameras[i].DetachDevice();
            return FALSE;
        }
        TRACE(_T("TCP communicator connected for camera %d.\n"), i);

        try {
            m_Detectors[i] = cv::createBackgroundSubtractorMOG2();
            if (!m_Detectors[i]) { TRACE(_T("Warning: Failed to create MOG2 detector for cam %d\n"), i); }
        }
        catch (const cv::Exception& e) {
            TRACE(_T("OpenCV Exception creating MOG2 detector for cam %d: %s\n"), i, CString(e.what()));
            m_Detectors[i].release();
        }

        m_bManualTrigger[i] = FALSE;
        m_bThreadStopFlags[i] = FALSE;
        m_ThreadParams[i] = {};
        m_ThreadParams[i].nCameraIndex = i;
        m_ThreadParams[i].hNotifyWnd = hNotifyWnd;
        m_ThreadParams[i].pCamera = &m_Cameras[i];
        m_ThreadParams[i].pCommLock = m_Comms[i].GetLock();
        m_ThreadParams[i].pCommunicator = &m_Comms[i];
        m_ThreadParams[i].pDetector = m_Detectors[i] ? (void*)m_Detectors[i].get() : nullptr;
        m_ThreadParams[i].config = cfg;

        UpdateMotionSettings(i, cfg.bMotionEnabled, cfg.nMotionThreshold);

        unsigned tid;
        m_hGrabThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, GrabThread, &m_ThreadParams[i], 0, &tid);
        if (!m_hGrabThreads[i]) {
            TRACE(_T("Failed to create GrabThread for camera %d.\n"), i);
            DisconnectCamera(i);
            return FALSE;
        }
        TRACE(_T("GrabThread started for camera %d (TID: %u).\n"), i, tid);

        m_Cameras[i].StartGrabbing(GrabStrategy_LatestImageOnly);
        TRACE(_T("Pylon grabbing started for camera %d.\n"), i);

    }
    catch (const GenericException& e) {
        CString msg;
        msg.Format(_T("Pylon Error connecting Camera %d: %s"), i, CString(e.GetDescription()));
        AfxMessageBox(msg, MB_ICONERROR);
        DisconnectCamera(i);
        return FALSE;
    }
    catch (const std::exception& e) {
        CString msg;
        msg.Format(_T("Standard Exception connecting Camera %d: %s"), i, CString(e.what()));
        AfxMessageBox(msg, MB_ICONERROR);
        DisconnectCamera(i);
        return FALSE;
    }
    catch (...) {
        CString msg;
        msg.Format(_T("Unknown Exception connecting Camera %d"), i);
        AfxMessageBox(msg, MB_ICONERROR);
        DisconnectCamera(i);
        return FALSE;
    }

    TRACE(_T("Camera %d connected successfully.\n"), i);
    return TRUE;
}

void CCameraManager::DisconnectCamera(int i) {
    if (i < 0 || i >= MAX_CAMERAS) return;
    TRACE(_T("Disconnecting camera %d...\n"), i);

    if (m_hGrabThreads[i]) {
        TRACE(_T("Signaling GrabThread %d to stop...\n"), i);
        m_bThreadStopFlags[i] = TRUE;
        DWORD waitResult = WaitForSingleObject(m_hGrabThreads[i], 3000);
        if (waitResult == WAIT_TIMEOUT) {
            TRACE(_T("Warning: GrabThread %d timed out.\n"), i);
        }
        else { TRACE(_T("GrabThread %d terminated.\n"), i); }
        CloseHandle(m_hGrabThreads[i]);
        m_hGrabThreads[i] = nullptr;
    }
    m_bThreadStopFlags[i] = FALSE;

    try {
        if (m_Cameras[i].IsGrabbing()) { TRACE(_T("Stopping Pylon grabbing for camera %d...\n"), i); m_Cameras[i].StopGrabbing(); }
        if (m_Cameras[i].IsOpen()) { TRACE(_T("Closing camera %d...\n"), i); m_Cameras[i].Close(); }
        if (m_Cameras[i].IsPylonDeviceAttached()) { TRACE(_T("Detaching device for camera %d...\n"), i); m_Cameras[i].DetachDevice(); }
    }
    catch (const GenericException& e) { TRACE(_T("Pylon error during disconnect camera %d: %s\n"), i, CString(e.GetDescription())); }
    catch (...) { TRACE(_T("Unknown error during Pylon cleanup for camera %d.\n"), i); }

    if (m_Comms[i].IsConnected()) { TRACE(_T("Disconnecting TCP communicator for camera %d...\n"), i); m_Comms[i].Disconnect(); }

    if (m_Detectors[i]) { TRACE(_T("Releasing detector for camera %d...\n"), i); m_Detectors[i].release(); }

    m_bManualTrigger[i] = FALSE;

    TRACE(_T("Camera %d disconnected.\n"), i);
}

void CCameraManager::DisconnectAll() {
    TRACE(_T("Disconnecting all cameras...\n"));
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        DisconnectCamera(i);
    }
    TRACE(_T("All cameras disconnected.\n"));
}

BOOL CCameraManager::IsCameraConnected(int i) {
    if (i < 0 || i >= MAX_CAMERAS) return FALSE;
    return m_Cameras[i].IsPylonDeviceAttached() && m_Comms[i].IsConnected();
}

void CCameraManager::TriggerManualCapture(int i) {
    if (i >= 0 && i < MAX_CAMERAS) {
        TRACE(_T("Manual capture triggered for camera %d.\n"), i);
        m_bManualTrigger[i] = TRUE;
    }
}

void CCameraManager::UpdateMotionSettings(int i, BOOL en, int th) {
    if (i < 0 || i >= MAX_CAMERAS) return;
    m_ThreadParams[i].config.bMotionEnabled = en;
    m_ThreadParams[i].config.nMotionThreshold = th;
    TRACE(_T("Updated motion settings for camera %d: Enable=%d, Threshold=%d\n"), i, en, th);
}

// <<< 카메라 상태 확인 함수 구현 >>>
BOOL CCameraManager::IsCameraOpen(int idx)
{
    if (idx < 0 || idx >= MAX_CAMERAS) return FALSE;
    try {
        // IsPylonDeviceAttached()는 객체에 장치가 연결되었는지만 확인
        // Open() 상태를 확인하려면 IsOpen() 사용
        return m_Cameras[idx].IsOpen();
    }
    catch (const Pylon::GenericException& e) {
        TRACE(_T("Pylon Exception checking IsOpen for Cam %d: %s\n"), idx, CString(e.GetDescription()));
        return FALSE;
    }
    catch (...) {
        TRACE(_T("Unknown Exception checking IsOpen for Cam %d\n"), idx);
        return FALSE;
    }
}

BOOL CCameraManager::IsCameraGrabbing(int idx)
{
    if (idx < 0 || idx >= MAX_CAMERAS) return FALSE;
    try {
        // IsGrabbing은 카메라가 열려있을 때만 호출 가능
        if (!m_Cameras[idx].IsOpen()) return FALSE;
        return m_Cameras[idx].IsGrabbing();
    }
    catch (const Pylon::GenericException& e) {
        TRACE(_T("Pylon Exception checking IsGrabbing for Cam %d: %s\n"), idx, CString(e.GetDescription()));
        return FALSE; // 예외 발생 시 Grabbing 상태 아님
    }
    catch (...) {
        TRACE(_T("Unknown Exception checking IsGrabbing for Cam %d\n"), idx);
        return FALSE;
    }
}
// <<< --- 구현 끝 --- >>>

// --- GenICam 파라미터 접근 함수 ---
BOOL CCameraManager::GetParameterFloat(int nCamIndex, double& dValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CFloatPtr ptrFloat(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsReadable(ptrFloat)) { TRACE(_T("Node '%s' not readable cam %d.\n"), CString(szNodeName), nCamIndex); return FALSE; }
        dValue = ptrFloat->GetValue();
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error getting float '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::SetParameterFloat(int nCamIndex, double dValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CFloatPtr ptrFloat(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsWritable(ptrFloat)) { TRACE(_T("Node '%s' not writable cam %d.\n"), CString(szNodeName), nCamIndex); return FALSE; }

        double minVal = ptrFloat->GetMin();
        double maxVal = ptrFloat->GetMax();
        if (dValue < minVal) { TRACE(_T("Clamping float %.2f to min %.2f for '%s' cam %d.\n"), dValue, minVal, CString(szNodeName), nCamIndex); dValue = minVal; }
        if (dValue > maxVal) { TRACE(_T("Clamping float %.2f to max %.2f for '%s' cam %d.\n"), dValue, maxVal, CString(szNodeName), nCamIndex); dValue = maxVal; }

        ptrFloat->SetValue(dValue);
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error setting float '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::GetParameterInt(int nCamIndex, int64_t& nValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CIntegerPtr ptrInt(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsReadable(ptrInt)) { TRACE(_T("Node '%s' not readable cam %d.\n"), CString(szNodeName), nCamIndex); return FALSE; }
        nValue = ptrInt->GetValue();
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error getting int '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::SetParameterInt(int nCamIndex, int64_t nValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CIntegerPtr ptrInt(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsWritable(ptrInt)) { TRACE(_T("Node '%s' not writable cam %d.\n"), CString(szNodeName), nCamIndex); return FALSE; }

        int64_t minVal = ptrInt->GetMin();
        int64_t maxVal = ptrInt->GetMax();
        if (nValue < minVal) nValue = minVal;
        if (nValue > maxVal) nValue = maxVal;

        ptrInt->SetValue(nValue);
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error setting int '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::GetParameterEnum(int nCamIndex, CString& sValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CEnumerationPtr ptrEnum(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsReadable(ptrEnum)) { TRACE(_T("Node '%s' not readable cam %d.\n"), CString(szNodeName), nCamIndex); return FALSE; }
        sValue = CString(ptrEnum->GetCurrentEntry()->GetSymbolic());
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error getting enum '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::SetParameterEnum(int nCamIndex, const CString& sValue, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CEnumerationPtr ptrEnum(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsWritable(ptrEnum)) {
            TRACE(_T("Node '%s' not writable for camera %d.\n"), CString(szNodeName), nCamIndex);
            return FALSE;
        }

        USES_CONVERSION;
        String_t valToSet = String_t(T2A(sValue));

        IEnumEntry* pEntry = ptrEnum->GetEntryByName(valToSet);
        if (!pEntry || !IsAvailable(pEntry))
        {
            TRACE(_T("Enum entry '%s' not found or not available for node '%s' cam %d.\n"), sValue, CString(szNodeName), nCamIndex);
            return FALSE;
        }

        ptrEnum->FromString(valToSet);
        return TRUE;
    }
    catch (const GenericException& e) {
        TRACE(_T("Pylon Error setting enum node '%s' for camera %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription()));
        return FALSE;
    }
    catch (...) { return FALSE; }
}

BOOL CCameraManager::ExecuteCommand(int nCamIndex, const char* szNodeName)
{
    if (nCamIndex < 0 || nCamIndex >= MAX_CAMERAS || !m_Cameras[nCamIndex].IsOpen()) return FALSE;
    INodeMap* pNodeMap = GetSafeNodeMap(&m_Cameras[nCamIndex]);
    if (!pNodeMap) return FALSE;

    try {
        CCommandPtr ptrCommand(pNodeMap->GetNode(String_t(szNodeName)));
        if (!IsWritable(ptrCommand)) {
            TRACE(_T("Command node '%s' not writable cam %d.\n"), CString(szNodeName), nCamIndex);
            return FALSE;
        }
        ptrCommand->Execute();
        return TRUE;
    }
    catch (const GenericException& e) { TRACE(_T("Pylon Error executing command '%s' cam %d: %s\n"), CString(szNodeName), nCamIndex, CString(e.GetDescription())); return FALSE; }
    catch (...) { return FALSE; }
}


// Grab 스레드 함수 (이전 답변의 안정성 강화 버전 사용)
UINT __cdecl CCameraManager::GrabThread(LPVOID p) {
    auto* prm = static_cast<GrabThreadParams*>(p);
    if (!prm) return 1;

    const int cam = prm->nCameraIndex;
    auto* pCam = prm->pCamera;
    auto* pComm = static_cast<CTcpCommunicator*>(prm->pCommunicator);
    cv::BackgroundSubtractorMOG2* pDet = static_cast<cv::BackgroundSubtractorMOG2*>(prm->pDetector);
    auto* lk = prm->pCommLock;

    auto* frameWnd = dynamic_cast<CFrameWnd*>(AfxGetMainWnd());
    auto* mainFrm = dynamic_cast<class CMainFrame*>(frameWnd);
    CCameraManager* mgr = mainFrm ? mainFrm->GetCameraManager() : nullptr;

    if (!pCam || !pComm || !lk || !mgr) { // pDet는 NULL일 수 있으므로 제외
        if (prm->hNotifyWnd) ::PostMessage(prm->hNotifyWnd, WM_CAMERA_STATUS, cam, (LPARAM)new CString(_T("Thread init error: Critical objects missing")));
        return 2;
    }

    CPylonImage pimg; CGrabResultPtr grab; CImageFormatConverter cvt; cvt.OutputPixelFormat = PixelType_BGR8packed;

    while (mgr && !mgr->m_bThreadStopFlags[cam]) {
        try {
            if (!pCam || !pCam->IsGrabbing())
            {
                if (prm->hNotifyWnd) ::PostMessage(prm->hNotifyWnd, WM_CAMERA_STATUS, cam, (LPARAM)new CString(_T("Camera not grabbing")));
                Sleep(1000);
                continue;
            }

            if (!pCam->RetrieveResult(1000, grab, TimeoutHandling_Return)) { continue; }
            if (!grab.IsValid()) { continue; }
            if (!grab->GrabSucceeded()) {
                TRACE(_T("Grab Failed Cam %d: Code %u, Desc: %s\n"), cam, grab->GetErrorCode(), CString(grab->GetErrorDescription()));
                continue;
            }

            cvt.Convert(pimg, grab);
            if (!pimg.IsValid()) { TRACE(_T("Image conversion failed for Cam %d\n"), cam); continue; }

            cv::Mat frm;
            try {
                frm = cv::Mat((int)pimg.GetHeight(), (int)pimg.GetWidth(), CV_8UC3, (uint8_t*)pimg.GetBuffer());
            }
            catch (const cv::Exception& e) {
                TRACE(_T("cv::Mat creation exception Cam %d: %s\n"), cam, CString(e.what())); continue;
            }
            catch (...) { TRACE(_T("Unknown exception during cv::Mat creation Cam %d\n"), cam); continue; }
            if (frm.empty()) { TRACE(_T("Created cv::Mat is empty for Cam %d\n"), cam); continue; }

            // 화면 송출
            if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                cv::Mat* copy = nullptr;
                try {
                    copy = new cv::Mat(frm.clone());
                    if (!::PostMessage(prm->hNotifyWnd, WM_UPDATE_FRAME, cam, (LPARAM)copy)) {
                        delete copy; copy = nullptr; TRACE(_T("PostMessage WM_UPDATE_FRAME failed for Cam %d\n"), cam);
                    }
                }
                catch (const cv::Exception& ex) {
                    delete copy; TRACE(_T("cv::Mat clone exception Cam %d: %s\n"), cam, CString(ex.what()));
                }
                catch (...) {
                    delete copy; TRACE(_T("Unknown exception during cv::Mat clone or PostMessage Cam %d.\n"), cam);
                }
            }

            // 전송 조건
            BOOL send = FALSE;
            if (prm->config.bMotionEnabled && pDet) {
                try {
                    cv::Mat mask;
                    pDet->apply(frm, mask);
                    if (!mask.empty() && cv::countNonZero(mask) > prm->config.nMotionThreshold) { send = TRUE; }
                }
                catch (const cv::Exception& ex) {
                    TRACE(_T("Motion detection exception Cam %d: %s\n"), cam, CString(ex.what()));
                }
                catch (...) { TRACE(_T("Unknown exception during motion detection Cam %d.\n"), cam); }
            }

            if (mgr && mgr->m_bManualTrigger[cam]) { send = TRUE; mgr->m_bManualTrigger[cam] = FALSE; }

            // 이미지 전송
            if (send && pComm && pComm->IsConnected()) {
                DWORD ts = GetTickCount();
                std::vector<uchar> jpg;
                try {
                    if (cv::imencode(".jpg", frm, jpg, { cv::IMWRITE_JPEG_QUALITY, 90 }) && !jpg.empty()) {
                        CSingleLock lock(lk, TRUE);
                        if (lock.IsLocked()) { pComm->SendImage(jpg.data(), (int)jpg.size(), ts); }
                        else { TRACE(_T("Failed to lock communicator for sending image Cam %d\n"), cam); }
                    }
                    else { TRACE(_T("JPEG encoding failed or resulted in empty data Cam %d\n"), cam); }
                }
                catch (const cv::Exception& ex) {
                    TRACE(_T("JPEG encoding exception Cam %d: %s\n"), cam, CString(ex.what()));
                }
                catch (...) { TRACE(_T("Unknown exception during JPEG encoding or sending Cam %d.\n"), cam); }
            }

            // 서버 결과 수신
            if (pComm && pComm->IsConnected()) {
                nlohmann::json js;
                try {
                    while (pComm->TryPopResult(js)) {
                        InspectionResult* r = nullptr;
                        try {
                            r = new InspectionResult{};
                            r->nCameraIndex = cam;
                            r->sProduct = CString(js.value("product", "").c_str());
                            r->sFinalResult = CString(js.value("final", "").c_str());
                            r->sTime = CString(js.value("time", "").c_str());
                            r->sDefectSummary = CString(js.value("summary", "").c_str());
                            r->dwTimestamp = GetTickCount();

                            if (js.contains("items") && js["items"].is_array()) {
                                for (auto& it : js["items"]) {
                                    if (!it.is_object()) continue;
                                    DefectItem di;
                                    di.sCameraType = CString(it.value("cam", "").c_str());
                                    di.sDefectType = CString(it.value("type", "").c_str());
                                    di.sDefectValue = CString(it.value("value", "").c_str());
                                    di.fConfidence = it.value("conf", 0.0f);
                                    di.sAdditionalInfo = CString(it.value("info", "").c_str());
                                    r->vecDefects.push_back(std::move(di));
                                }
                            }

                            if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                                if (!::PostMessage(prm->hNotifyWnd, WM_INSPECTION_RESULT, 0, (LPARAM)r)) {
                                    delete r; r = nullptr; TRACE(_T("PostMessage WM_INSPECTION_RESULT failed for Cam %d\n"), cam);
                                }
                            }
                            else { delete r; r = nullptr; }
                        }
                        catch (const std::bad_alloc&) { TRACE(_T("Memory allocation failed for InspectionResult Cam %d\n"), cam); }
                        catch (...) { delete r; TRACE(_T("Unknown exception during InspectionResult processing Cam %d\n"), cam); }
                    } // end while
                }
                catch (const nlohmann::json::exception& e) {
                    TRACE(_T("JSON parsing exception Cam %d: %s\n"), cam, CString(e.what()));
                }
                catch (...) { TRACE(_T("Unknown exception during result processing loop Cam %d.\n"), cam); }
            } // end if pComm

        } // end try (main grab loop)
        catch (const GenericException& e) {
            TRACE(_T("Pylon GenericException in GrabThread Cam %d: %s\n"), cam, CString(e.GetDescription()));
            if (prm->hNotifyWnd) ::PostMessage(prm->hNotifyWnd, WM_CAMERA_DISCONNECTED, cam, 0);
            break; // 스레드 종료
        }
        catch (const std::exception& e) { TRACE(_T("std::exception in GrabThread Cam %d: %s\n"), cam, CString(e.what())); Sleep(100); }
        catch (...) { TRACE(_T("Unknown exception in GrabThread Cam %d.\n"), cam); Sleep(1000); }
    } // end while

    TRACE(_T("GrabThread Cam %d finished.\n"), cam);
    return 0;
}