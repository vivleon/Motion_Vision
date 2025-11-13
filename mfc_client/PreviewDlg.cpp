#include "pch.h"
#include "CanClient.h"
#include "afxdialogex.h"
#include "PreviewDlg.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

// GDI+
#include <gdiplus.h>
using namespace Gdiplus;
// GDI+ Token is now managed by CCanClientDlg

// CPreviewDlg Implementation
IMPLEMENT_DYNAMIC(CPreviewDlg, CDialogEx)

CPreviewDlg::CPreviewDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_PREVIEW_DLG, pParent)
    , m_imgTop(nullptr)
    , m_imgSide(nullptr)
{
}

CPreviewDlg::~CPreviewDlg()
{
    // Clean up GDI+ images
    if (m_imgTop) delete m_imgTop;
    if (m_imgSide) delete m_imgSide;
}

// [NEW] Set paths
void CPreviewDlg::SetImagePaths(CString strPathTop, CString strPathSide)
{
    m_strPathTop = strPathTop;
    m_strPathSide = strPathSide;
}

// ========================================================================
// [MODIFIED] LoadImageFromFile (멈춤 현상 수정)
// ========================================================================
void CPreviewDlg::LoadImageFromFile(CString sPath, Gdiplus::Image** ppImage)
{
    if (sPath.IsEmpty()) return;

    if (*ppImage) { delete* ppImage; *ppImage = nullptr; }

    // 1) 경로 유효성 체크
    if (!PathFileExists(sPath)) {
        AfxTrace(L"[Preview] 파일이 존재하지 않습니다: %s\n", sPath);
        return;
    }

    // 2) GDI+ 로드 (유니코드 경로 그대로)
    *ppImage = Gdiplus::Image::FromFile(sPath);
    if (!*ppImage || (*ppImage)->GetLastStatus() != Gdiplus::Ok)
    {
        AfxTrace(L"[Preview] GDI+ 로드 실패: %s (status=%d)\n",
            sPath, *ppImage ? (*ppImage)->GetLastStatus() : -1);
        if (*ppImage) { delete* ppImage; *ppImage = nullptr; }
        return;
    }
}


void CPreviewDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// [NEW] Load images on initialization
BOOL CPreviewDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    LoadImageFromFile(m_strPathTop, &m_imgTop);
    LoadImageFromFile(m_strPathSide, &m_imgSide);

    // 두 이미지 영역 다시 그리기 트리거
    if (CWnd* pL = GetDlgItem(IDC_IMG_LEFT))  pL->Invalidate();
    if (CWnd* pR = GetDlgItem(IDC_IMG_RIGHT)) pR->Invalidate();
    Invalidate(); // 다이얼로그 전체

    return TRUE;
}

BEGIN_MESSAGE_MAP(CPreviewDlg, CDialogEx)
    ON_WM_PAINT()
END_MESSAGE_MAP()


// CPreviewDlg message handlers

// [NEW] OnPaint for drawing
void CPreviewDlg::OnPaint()
{
    CPaintDC dc(this); // device context for painting

    // Fill background
    CRect rcClient;
    GetClientRect(&rcClient);
    CBrush brBkg;
    //brBkg.CreateSolidBrush(RGB(30, 30, 30)); // Dark background
    dc.FillRect(rcClient, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));
    brBkg.DeleteObject();

    // Draw images
    DrawImageToCtrl(m_imgTop, IDC_IMG_LEFT);
    DrawImageToCtrl(m_imgSide, IDC_IMG_RIGHT);
}

void CPreviewDlg::DrawImageToCtrl(Gdiplus::Image* pImage, UINT nCtrlID)
{
    CWnd* pWnd = GetDlgItem(nCtrlID);
    if (!pWnd) return;

    CClientDC dc(pWnd);
    CRect rc;
    pWnd->GetClientRect(&rc);

    // Fill background first
    dc.FillRect(rc, CBrush::FromHandle(GetSysColorBrush(COLOR_BTNFACE)));

    if (!pImage) {
    // 회색 배경 위에 안내 텍스트
    dc.SetBkMode(TRANSPARENT);
    dc.DrawText(L"(이미지 없음)", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
}

    Graphics graphics(dc.GetSafeHdc());
    graphics.SetInterpolationMode(InterpolationModeHighQuality);

    // Calculate aspect ratio
    RectF rcDraw;
    REAL srcAR = (REAL)pImage->GetWidth() / pImage->GetHeight();
    REAL dstAR = (REAL)rc.Width() / rc.Height();

    if (srcAR > dstAR) {
        // Source is wider than destination
        rcDraw.Width = (REAL)rc.Width();
        rcDraw.Height = rc.Width() / srcAR;
        rcDraw.X = 0;
        rcDraw.Y = (rc.Height() - rcDraw.Height) / 2;
    }
    else {
        // Source is taller than destination
        rcDraw.Height = (REAL)rc.Height();
        rcDraw.Width = rc.Height() * srcAR;
        rcDraw.Y = 0;
        rcDraw.X = (rc.Width() - rcDraw.Width) / 2;
    }

    // Draw the image
    graphics.DrawImage(pImage, rcDraw, 0, 0, (REAL)pImage->GetWidth(), (REAL)pImage->GetHeight(), UnitPixel);
}

void CPreviewDlg::PostNcDestroy()
{
    CDialogEx::PostNcDestroy();
    if (m_bModeless) delete this;
}
