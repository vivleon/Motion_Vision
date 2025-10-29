#pragma once
// #include <pylon/PylonIncludes.h> // pch.h에 이미 포함 가정
// #include <opencv2/opencv.hpp> // pch.h에 이미 포함 가정
#include "SharedData.h" // CameraConfig 등 필요
#include "CTcpCommunicator.h" // CTcpCommunicator 정의 필요

namespace Pylon {
    class CDeviceInfo; // Forward 선언
}

class CCameraManager {
public:
    CCameraManager();
    ~CCameraManager();

    // 기존 public 함수들
    void FindDevices(std::vector<Pylon::CDeviceInfo>& out);
    BOOL ConnectCamera(const CameraConfig& cfg, HWND hNotifyWnd);
    void DisconnectCamera(int idx);
    void DisconnectAll();
    BOOL IsCameraConnected(int idx); // TCP 연결 포함 확인
    void TriggerManualCapture(int idx);
    void UpdateMotionSettings(int idx, BOOL enable, int threshold);

    // GenICam 파라미터 접근 함수들
    BOOL GetParameterFloat(int nCamIndex, double& dValue, const char* szNodeName);
    BOOL SetParameterFloat(int nCamIndex, double dValue, const char* szNodeName);
    BOOL GetParameterInt(int nCamIndex, int64_t& nValue, const char* szNodeName);
    BOOL SetParameterInt(int nCamIndex, int64_t nValue, const char* szNodeName);
    BOOL GetParameterEnum(int nCamIndex, CString& sValue, const char* szNodeName);
    BOOL SetParameterEnum(int nCamIndex, const CString& sValue, const char* szNodeName);
    BOOL ExecuteCommand(int nCamIndex, const char* szNodeName);

    // <<< 카메라 상태 확인 위한 public 함수 추가 >>>
    BOOL IsCameraOpen(int idx);
    BOOL IsCameraGrabbing(int idx);
    // <<< --- 추가 끝 --- >>>

// public 멤버로 이동 (GrabThread에서 접근 필요) -> 원래 위치 유지해도 GrabThread는 friend 처리가 필요 없었음
// GrabThread는 static 멤버 함수이므로 멤버 변수 직접 접근 시 객체 포인터 필요
// CCameraManager* mgr = (CCameraManager*)pParam->pManager; // 이런 식으로 전달받아 사용
public: // GrabThread needs access via object pointer passed in params
    HANDLE m_hGrabThreads[MAX_CAMERAS];
    BOOL   m_bThreadStopFlags[MAX_CAMERAS];
    GrabThreadParams m_ThreadParams[MAX_CAMERAS];
    cv::Ptr<cv::BackgroundSubtractorMOG2> m_Detectors[MAX_CAMERAS];
    BOOL   m_bManualTrigger[MAX_CAMERAS];

private:
    Pylon::PylonAutoInitTerm m_pylonAutoInitTerm;
    Pylon::CInstantCamera m_Cameras[MAX_CAMERAS]; // <<< private 유지
    CTcpCommunicator m_Comms[MAX_CAMERAS];

    // Grab 스레드 함수는 static이므로 클래스 멤버로 유지
    static UINT __cdecl GrabThread(LPVOID pParam);
};

