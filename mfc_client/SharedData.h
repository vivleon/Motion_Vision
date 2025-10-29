#pragma once
#include <afxmt.h> 
#include <vector> // Keep standard headers if needed specifically here, though likely in pch.h
#include <opencv2/opencv.hpp>
#include <string>
#include <list>
#include <afxwin.h> // CWnd, HWND 등
#include <pylon/PylonIncludes.h> // Pylon::CInstantCamera 사용 위해 추가

// Forward declare Pylon types if full definition not needed, otherwise ensure pch.h includes them
namespace Pylon {
    class CInstantCamera;
}
// Forward declare OpenCV types
namespace cv {
    class Mat;
    class BackgroundSubtractorMOG2; // Forward declare if Ptr<> is used
    template<typename _Tp> class Ptr; // Forward declare Ptr template
}

#define MAX_CAMERAS 4

// App <-> View <-> Threads 메시지
#define WM_UPDATE_FRAME         (WM_USER + 101) // (WPARAM) camIndex, (LPARAM) cv::Mat*
#define WM_INSPECTION_RESULT    (WM_USER + 102) // (WPARAM) 0, (LPARAM) InspectionResult*
#define WM_CAMERA_STATUS        (WM_USER + 103) // (WPARAM) camIndex, (LPARAM) CString*
#define WM_CAMERA_DISCONNECTED  (WM_USER + 104) // (WPARAM) camIndex
#define WM_TCP_STATUS           (WM_USER + 105) // (WPARAM) camIndex, (LPARAM) BOOL

// DB 스키마와 매핑되는 JSON 결과 항목
struct DefectItem {
    CString sCameraType;    // items[].cam    -> 'top' | 'side'
    CString sDefectType;    // items[].type   -> '뚜껑유무' 등
    CString sDefectValue;   // items[].value  -> '정상' | '불량'
    float   fConfidence;    // items[].conf
    CString sAdditionalInfo;// items[].info
};

struct InspectionResult {
    int     nCameraIndex{};     // 클라이언트 기준
    CString sProduct;            // product        (품목번호 등)
    CString sFinalResult;        // final          ('정상'|'불량')
    CString sTime;               // time           (ISO string)
    CString sDefectSummary;      // summary
    DWORD   dwTimestamp{};      // 로컬 매칭용
    std::vector<DefectItem> vecDefects; // items[]
};

// 카메라 설정
struct CameraConfig {
    int     nIndex = -1;         // 0-based index
    CString sSerial;           // 카메라 시리얼 번호 (Pylon 식별용)
    CString sFriendlyName;     // 사용자 지정 이름 (UI 표시용)
    CString sIp;               // TCP 서버 IP 주소
    int     nPort = 9000;      // TCP 서버 포트
    BOOL    bMotionEnabled = TRUE; // 모션 감지 사용 여부
    int     nMotionThreshold = 5000; // 모션 감지 임계값
    double  dExposureTime = 10000.0; // 노출 시간 (us) - 기본값 예시
    double  dGain = 1.0;            // 게인 값 - 기본값 예시
};

// 스레드 파라미터
struct GrabThreadParams {
    int nCameraIndex{};
    HWND hNotifyWnd{};
    CCriticalSection* pCommLock{};
    Pylon::CInstantCamera* pCamera{}; // Pointer, so forward declaration is okay
    void* pCommunicator{}; // CTcpCommunicator* (Forward declare or include CTcpCommunicator.h *after* this struct if needed by value)
    // Use void* or forward declare cv::BackgroundSubtractorMOG2 if definition isn't required here
    // cv::Ptr<cv::BackgroundSubtractorMOG2> needs the full definition usually.
    // Let's assume void* is sufficient for the struct definition itself.
    void* pDetector{};     // cv::BackgroundSubtractorMOG2* or cv::Ptr<cv::BackgroundSubtractorMOG2>*
    CameraConfig config;
};

// 불량 저장
struct DefectData {
    cv::Mat matImage; // Needs full cv::Mat definition (via pch.h)
    InspectionResult result;
};
