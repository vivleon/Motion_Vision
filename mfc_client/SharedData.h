#pragma once
#include <afxmt.h> 
#include <vector>
#include <opencv2/opencv.hpp>
#include <string>
#include <list>
#include <afxwin.h> // CWnd, HWND 등
#include <pylon/PylonIncludes.h> // Pylon::CInstantCamera 사용 위해 추가

// Pylon/OpenCV 네임스페이스 Forward 선언 (변경 없음)
namespace Pylon {
    class CInstantCamera;
}
namespace cv {
    class Mat;
    class BackgroundSubtractorMOG2;
    template<typename _Tp> class Ptr;
}

#define MAX_CAMERAS 4

// App <-> View <-> Threads 메시지
// (기존 메시지 정의는 유지)
#define WM_UPDATE_FRAME         (WM_USER + 101) // (WPARAM) camIndex, (LPARAM) cv::Mat*
#define WM_INSPECTION_RESULT    (WM_USER + 102) // (WPARAM) 0, (LPARAM) InspectionResult*
#define WM_CAMERA_STATUS        (WM_USER + 103) // (WPARAM) camIndex, (LPARAM) CString*
#define WM_CAMERA_DISCONNECTED  (WM_USER + 104) // (WPARAM) camIndex
#define WM_TCP_STATUS           (WM_USER + 105) // (WPARAM) camIndex, (LPARAM) BOOL

// <<< 상태 표시줄 업데이트용 메시지 추가 >>>
#define WM_UPDATE_STATUS_PANE   (WM_USER + 106) // (WPARAM) Pane ID, (LPARAM) CString*
// <<< --- 추가 끝 --- >>>


// 구조체 정의 (DefectItem, InspectionResult, CameraConfig, GrabThreadParams, DefectData)
// (변경 없이 기존 내용 유지)

struct DefectItem {
    CString sCameraType;    // items[].cam    -> 'top' | 'side'
    CString sDefectType;    // items[].type   -> '뚜껑유무' 등
    CString sDefectValue;   // items[].value  -> '정상' | '불량'
    float   fConfidence;    // items[].conf
    CString sAdditionalInfo;// items[].info
};

struct InspectionResult {
    int     nCameraIndex{};     // 클라이언트 기준
    CString sProduct;           // product         (품목번호 등)
    CString sFinalResult;       // final           ('정상'|'불량')
    CString sTime;              // time            (ISO string)
    CString sDefectSummary;     // summary
    DWORD   dwTimestamp{};      // 로컬 매칭용
    std::vector<DefectItem> vecDefects; // items[]
};

struct CameraConfig {
    int     nIndex = -1;         // 0-based index
    CString sSerial;           // 카메라 시리얼 번호 (Pylon 식별용)
    CString sFriendlyName;     // 사용자 지정 이름 (UI 표시용)
    CString sIp;               // TCP 서버 IP 주소
    int     nPort = 9000;      // TCP 서버 포트
    BOOL    bMotionEnabled = TRUE; // 모션 감지 사용 여부
    int     nMotionThreshold = 5000; // 모션 감지 임계값
    double  dExposureTime = 10000.0; // 노출 시간 (us)
    double  dGain = 1.0;            // 게인 값
};

// ... (GrabThreadParams 구조체) ...
// CCameraManager의 GrabThread 스레드에 전달될 파라미터
struct GrabThreadParams
{
    int nCameraIndex = -1;      // 카메라 인덱스
    HWND hNotifyWnd = nullptr;  // 알림을 받을 윈도우 핸들 (메인 프레임)
    Pylon::CInstantCamera* pCamera = nullptr; // Pylon 카메라 객체 포인터
    CCriticalSection* pCommLock = nullptr;    // TCP 통신 객체 잠금용
    void* pCommunicator = nullptr; // CTcpCommunicator 객체 포인터 (실제 타입은 cpp에서 캐스팅)
    void* pDetector = nullptr;     // cv::BackgroundSubtractorMOG2 객체 포인터
    CameraConfig config;        // 해당 카메라의 설정 (스레드 시작 시 복사)
};

// ... (DefectData 구조체) ...
struct DefectData
{
    InspectionResult result;
    cv::Mat matImage; // 불량 발생 시점의 이미지 (JPEG 저장용)
};
