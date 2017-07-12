#include "stdafx.h"
#include <assert.h>
#include <strsafe.h>
#include <math.h>
#include <limits>
#include <Wincodec.h>
#include "resource.h"
#include "CoordinateMappingBasics.h"

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    CoInitialize(nullptr);

    CCoordinateMappingBasics application;
    application.Run(hInstance, nShowCmd);

    CoUninitialize();
}

CCoordinateMappingBasics::CCoordinateMappingBasics() :
    m_hWnd(nullptr),
    m_nStartTime(0),
    m_nLastCounter(0),
    m_nFramesSinceUpdate(0),
    m_fFreq(0),
    m_nNextStatusTime(0LL),
    m_bSaveScreenshot(false),
    m_pKinectSensor(nullptr),
    m_pCoordinateMapper(nullptr),
    m_pMultiSourceFrameReader(nullptr),
    m_pD2DFactory(nullptr)
{
    LARGE_INTEGER qpf = {0};
    if (QueryPerformanceFrequency(&qpf))
    {
        m_fFreq = double(qpf.QuadPart);
    }

    // create heap storage for composite image pixel data in RGBX format
    m_pOutputRGBX = std::make_unique<RGBQUAD[]>(cColorWidth * cColorHeight);

    // create heap storage for background image pixel data in RGBX format
    m_pBackgroundRGBX = std::make_unique<RGBQUAD[]>(cColorWidth * cColorHeight);

    // create heap storage for color pixel data in RGBX format
    m_pColorRGBX = std::make_unique<RGBQUAD[]>(cColorWidth * cColorHeight);

    // create heap storage for the coorinate mapping from color to depth
    m_pDepthCoordinates = std::make_unique<DepthSpacePoint[]>(cColorWidth * cColorHeight);
}
  
CCoordinateMappingBasics::~CCoordinateMappingBasics()
{
    // close the Kinect Sensor
    if (m_pKinectSensor)
    {
        m_pKinectSensor->Close();
    }
}

int CCoordinateMappingBasics::Run(
    HINSTANCE hInstance,
    int nCmdShow)
{
    if (m_pBackgroundRGBX)
    {
        if (FAILED(LoadResourceImage(
            L"Background",
            L"Image",
            cColorWidth,
            cColorHeight,
            m_pBackgroundRGBX.get())))
        {
            const RGBQUAD c_green = {0, 255, 0}; 

            // Fill in with a background colour of green if we can't load the background image
            for (int i = 0 ; i < cColorWidth * cColorHeight ; ++i)
            {
                m_pBackgroundRGBX[i] = c_green;
            }
        }
    }

    MSG       msg = {0};
    WNDCLASS  wc;

    // Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"CoordinateMappingBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        nullptr,
        MAKEINTRESOURCE(IDD_APP),
        nullptr,
        (DLGPROC)CCoordinateMappingBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Show window
    ShowWindow(hWndApp, nCmdShow);

    // Main message loop
    while (WM_QUIT != msg.message)
    {
        Update();

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if (hWndApp && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

void CCoordinateMappingBasics::Update()
{
    V_CHECK(m_pMultiSourceFrameReader != nullptr);

    Microsoft::WRL::ComPtr<IMultiSourceFrame> pMultiSourceFrame;
    V(m_pMultiSourceFrameReader->AcquireLatestFrame(&pMultiSourceFrame));

    // Get the depth frame data
    Microsoft::WRL::ComPtr<IDepthFrameReference> pDepthFrameReference;
    V(pMultiSourceFrame->get_DepthFrameReference(&pDepthFrameReference));

    Microsoft::WRL::ComPtr<IDepthFrame> pDepthFrame;
    V(pDepthFrameReference->AcquireFrame(&pDepthFrame));

    int64_t nDepthTime = 0;
    V(pDepthFrame->get_RelativeTime(&nDepthTime));

    Microsoft::WRL::ComPtr<IFrameDescription> pDepthFrameDescription;
    V(pDepthFrame->get_FrameDescription(&pDepthFrameDescription));

    int nDepthWidth = 0;
    V(pDepthFrameDescription->get_Width(&nDepthWidth));

    int nDepthHeight = 0;
    V(pDepthFrameDescription->get_Height(&nDepthHeight));

    UINT16 *pDepthBuffer = nullptr;
    UINT nDepthBufferSize = 0;
    V(pDepthFrame->AccessUnderlyingBuffer(&nDepthBufferSize, &pDepthBuffer));

    // Get the color frame data
    Microsoft::WRL::ComPtr<IColorFrameReference> pColorFrameReference;
    V(pMultiSourceFrame->get_ColorFrameReference(&pColorFrameReference));

    Microsoft::WRL::ComPtr<IColorFrame> pColorFrame;
    V(pColorFrameReference->AcquireFrame(&pColorFrame));


    // get color frame data
    Microsoft::WRL::ComPtr<IFrameDescription> pColorFrameDescription;
    V(pColorFrame->get_FrameDescription(&pColorFrameDescription));

    int nColorWidth = 0;
    V(pColorFrameDescription->get_Width(&nColorWidth));

    int nColorHeight = 0;
    V(pColorFrameDescription->get_Height(&nColorHeight));

    ColorImageFormat imageFormat = ColorImageFormat_None;
    V(pColorFrame->get_RawColorImageFormat(&imageFormat));

    UINT nColorBufferSize = 0;
    RGBQUAD *pColorBuffer = nullptr;
    if (imageFormat == ColorImageFormat_Bgra)
    {
        V(pColorFrame->AccessRawUnderlyingBuffer(&nColorBufferSize, reinterpret_cast<BYTE**>(&pColorBuffer)));
    }
    else if (m_pColorRGBX)
    {
        pColorBuffer = m_pColorRGBX.get();
        nColorBufferSize = cColorWidth * cColorHeight * sizeof(RGBQUAD);
        V(pColorFrame->CopyConvertedFrameDataToArray(nColorBufferSize, reinterpret_cast<BYTE*>(pColorBuffer), ColorImageFormat_Bgra));
    }
    else
    {
        assert(false);
        return;
    }

    // Get the body index frame data
    Microsoft::WRL::ComPtr<IBodyIndexFrameReference> pBodyIndexFrameReference;
    V(pMultiSourceFrame->get_BodyIndexFrameReference(&pBodyIndexFrameReference));

    Microsoft::WRL::ComPtr<IBodyIndexFrame> pBodyIndexFrame;
    V(pBodyIndexFrameReference->AcquireFrame(&pBodyIndexFrame));

    Microsoft::WRL::ComPtr<IFrameDescription> pBodyIndexFrameDescription;
    V(pBodyIndexFrame->get_FrameDescription(&pBodyIndexFrameDescription));

    int nBodyIndexWidth = 0;
    V(pBodyIndexFrameDescription->get_Width(&nBodyIndexWidth));

    int nBodyIndexHeight = 0;
    V(pBodyIndexFrameDescription->get_Height(&nBodyIndexHeight));

    UINT nBodyIndexBufferSize = 0;
    BYTE* pBodyIndexBuffer = nullptr;
    V(pBodyIndexFrame->AccessUnderlyingBuffer(&nBodyIndexBufferSize, &pBodyIndexBuffer));            

    // TODO: [thperry] Combine them into objects with buffer pointer, width and height.
    ProcessFrame(
        nDepthTime,
        pDepthBuffer,
        nDepthWidth,
        nDepthHeight, 
        pColorBuffer,
        nColorWidth,
        nColorHeight,
        pBodyIndexBuffer,
        nBodyIndexWidth,
        nBodyIndexHeight);
}

LRESULT CALLBACK CCoordinateMappingBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CCoordinateMappingBasics* pThis = nullptr;
    
    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CCoordinateMappingBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CCoordinateMappingBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

LRESULT CALLBACK CCoordinateMappingBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
        case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            ID2D1Factory* rawD2DFactory = nullptr;
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &rawD2DFactory);
            m_pD2DFactory.Attach(rawD2DFactory);

            // Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
            // We'll use this to draw the data we receive from the Kinect to the screen
            m_pDrawCoordinateMapping = std::make_unique<ImageRenderer>(); 
            HRESULT hr = m_pDrawCoordinateMapping->Initialize(
                GetDlgItem(m_hWnd, IDC_VIDEOVIEW),
                m_pD2DFactory.Get(),
                cColorWidth,
                cColorHeight,
                cColorWidth * sizeof(RGBQUAD));
            if (FAILED(hr))
            {
                SetStatusMessage(L"Failed to initialize the Direct2D draw device.", 10000, true);
            }

            // Get and initialize the default Kinect sensor
            InitializeDefaultSensor();
        }
        break;

        // If the titlebar X is clicked, destroy app
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            // Quit the main message pump
            PostQuitMessage(0);
            break;

        // Handle button press
        case WM_COMMAND:
            // If it was for the screenshot control and a button clicked event, save a screenshot next frame 
            if (IDC_BUTTON_SCREENSHOT == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
            {
                m_bSaveScreenshot = true;
            }
            break;
    }

    return FALSE;
}

HRESULT CCoordinateMappingBasics::InitializeDefaultSensor()
{
    HRESULT hr = S_OK;

    hr = GetDefaultKinectSensor(&m_pKinectSensor);
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_pKinectSensor)
    {
        // Initialize the Kinect and get coordinate mapper and the frame reader

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
        }

        hr = m_pKinectSensor->Open();

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->OpenMultiSourceFrameReader(
                FrameSourceTypes::FrameSourceTypes_Depth | FrameSourceTypes::FrameSourceTypes_Color | FrameSourceTypes::FrameSourceTypes_BodyIndex,
                &m_pMultiSourceFrameReader);
        }
    }

    if (!m_pKinectSensor || FAILED(hr))
    {
        SetStatusMessage(L"No ready Kinect found!", 10000, true);
        return E_FAIL;
    }

    return hr;
}

void CCoordinateMappingBasics::ProcessFrame(
    int64_t nTime, 
    const UINT16* pDepthBuffer,
    int nDepthWidth,
    int nDepthHeight, 
    const RGBQUAD* pColorBuffer,
    int nColorWidth,
    int nColorHeight,
    const BYTE* pBodyIndexBuffer,
    int nBodyIndexWidth,
    int nBodyIndexHeight)
{
    if (m_hWnd)
    {
        if (!m_nStartTime)
        {
            m_nStartTime = nTime;
        }

        double fps = 0.0;

        LARGE_INTEGER qpcNow = {0};
        if (m_fFreq)
        {
            if (QueryPerformanceCounter(&qpcNow))
            {
                if (m_nLastCounter)
                {
                    m_nFramesSinceUpdate++;
                    fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
                }
            }
        }

        WCHAR szStatusMessage[64];
        StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f    Time = %I64d", fps, (nTime - m_nStartTime));

        if (SetStatusMessage(szStatusMessage, 1000, false))
        {
            m_nLastCounter = qpcNow.QuadPart;
            m_nFramesSinceUpdate = 0;
        }
    }

    // Make sure we've received valid data
    V_CHECK(m_pCoordinateMapper && m_pDepthCoordinates && m_pOutputRGBX &&
        pDepthBuffer && (nDepthWidth == cDepthWidth) && (nDepthHeight == cDepthHeight) &&
        pColorBuffer && (nColorWidth == cColorWidth) && (nColorHeight == cColorHeight) &&
        pBodyIndexBuffer && (nBodyIndexWidth == cDepthWidth) && (nBodyIndexHeight == cDepthHeight));

    V(m_pCoordinateMapper->MapColorFrameToDepthSpace(
        nDepthWidth * nDepthHeight,
        (UINT16*)pDepthBuffer,
        nColorWidth * nColorHeight,
        m_pDepthCoordinates.get()));

    // loop over output pixels
    for (int colorIndex = 0; colorIndex < (nColorWidth*nColorHeight); ++colorIndex)
    {
        // default setting source to copy from the background pixel
        const RGBQUAD* pSrc = m_pBackgroundRGBX.get() + colorIndex;

        DepthSpacePoint p = m_pDepthCoordinates[colorIndex];

        // Values that are negative infinity means it is an invalid color to depth mapping so we
        // skip processing for this pixel
        if (p.X != -std::numeric_limits<float>::infinity() && p.Y != -std::numeric_limits<float>::infinity())
        {
            int depthX = static_cast<int>(p.X + 0.5f);
            int depthY = static_cast<int>(p.Y + 0.5f);

            if ((depthX >= 0 && depthX < nDepthWidth) && (depthY >= 0 && depthY < nDepthHeight))
            {
                BYTE player = pBodyIndexBuffer[depthX + (depthY * cDepthWidth)];

                // if we're tracking a player for the current pixel, draw from the color camera
                if (player != 0xff)
                {
                    // set source for copy to the color pixel
                    pSrc = m_pColorRGBX.get() + colorIndex;
                }
            }
        }

        // write output
        m_pOutputRGBX[colorIndex] = *pSrc;
    }

    // Draw the data with Direct2D
    V(m_pDrawCoordinateMapping->Draw(
        reinterpret_cast<BYTE*>(m_pOutputRGBX.get()),
        cColorWidth * cColorHeight * sizeof(RGBQUAD)));

    if (m_bSaveScreenshot)
    {
        WCHAR szScreenshotPath[MAX_PATH];

        // Retrieve the path to My Photos
        GetScreenshotFileName(szScreenshotPath, _countof(szScreenshotPath));

        // Write out the bitmap to disk
        HRESULT hr = SaveBitmapToFile(
            reinterpret_cast<BYTE*>(m_pOutputRGBX.get()),
            nColorWidth, nColorHeight,
            sizeof(RGBQUAD) * 8,
            szScreenshotPath);

        WCHAR szStatusMessage[64 + MAX_PATH];
        if (SUCCEEDED(hr))
        {
            // Set the status bar to show where the screenshot was saved
            StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L"Screenshot saved to %s", szScreenshotPath);
        }
        else
        {
            StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L"Failed to write screenshot to %s", szScreenshotPath);
        }

        SetStatusMessage(szStatusMessage, 5000, true);

        // toggle off so we don't save a screenshot again next frame
        m_bSaveScreenshot = false;
    }
}

bool CCoordinateMappingBasics::SetStatusMessage(
    _In_z_ WCHAR* szMessage,
    DWORD nShowTimeMsec,
    bool bForce)
{
    int64_t now = GetTickCount64();

    if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
    {
        SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
        m_nNextStatusTime = now + nShowTimeMsec;

        return true;
    }

    return false;
}

HRESULT CCoordinateMappingBasics::GetScreenshotFileName(
    _Out_writes_z_(nFilePathSize) LPWSTR lpszFilePath,
    UINT nFilePathSize)
{
    WCHAR* pszKnownPath = nullptr;
    V_RET(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pszKnownPath));

    // Get the time
    WCHAR szTimeString[MAX_PATH];
    GetTimeFormatEx(nullptr, 0, nullptr, L"hh'-'mm'-'ss", szTimeString, _countof(szTimeString));

    // File name will be KinectScreenshotDepth-HH-MM-SS.bmp
    StringCchPrintfW(lpszFilePath, nFilePathSize, L"%s\\KinectScreenshot-CoordinateMapping-%s.bmp", pszKnownPath, szTimeString);

    if (pszKnownPath)
    {
        // TODO: [thperry] Add RAII cleanup of this
        CoTaskMemFree(pszKnownPath);
    }

    return S_OK;
}

HRESULT CCoordinateMappingBasics::SaveBitmapToFile(
    BYTE* pBitmapBits,
    LONG lWidth,
    LONG lHeight,
    WORD wBitsPerPixel,
    LPCWSTR lpszFilePath)
{
    DWORD dwByteCount = lWidth * lHeight * (wBitsPerPixel / 8);

    BITMAPINFOHEADER bmpInfoHeader = {0};

    bmpInfoHeader.biSize        = sizeof(BITMAPINFOHEADER);  // Size of the header
    bmpInfoHeader.biBitCount    = wBitsPerPixel;             // Bit count
    bmpInfoHeader.biCompression = BI_RGB;                    // Standard RGB, no compression
    bmpInfoHeader.biWidth       = lWidth;                    // Width in pixels
    bmpInfoHeader.biHeight      = -lHeight;                  // Height in pixels, negative indicates it's stored right-side-up
    bmpInfoHeader.biPlanes      = 1;                         // Default
    bmpInfoHeader.biSizeImage   = dwByteCount;               // Image size in bytes

    BITMAPFILEHEADER bfh = {0};

    bfh.bfType    = 0x4D42;                                           // 'M''B', indicates bitmap
    bfh.bfOffBits = bmpInfoHeader.biSize + sizeof(BITMAPFILEHEADER);  // Offset to the start of pixel data
    bfh.bfSize    = bfh.bfOffBits + bmpInfoHeader.biSizeImage;        // Size of image + headers

    // Create the file on disk to write to
    HANDLE hFile = CreateFileW(lpszFilePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    // Return if error opening file
    if (nullptr == hFile) 
    {
        return E_ACCESSDENIED;
    }

    DWORD dwBytesWritten = 0;
    
    // Write the bitmap file header
    if (!WriteFile(hFile, &bfh, sizeof(bfh), &dwBytesWritten, nullptr))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }
    
    // Write the bitmap info header
    if (!WriteFile(hFile, &bmpInfoHeader, sizeof(bmpInfoHeader), &dwBytesWritten, nullptr))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }
    
    // Write the RGB Data
    if (!WriteFile(hFile, pBitmapBits, bmpInfoHeader.biSizeImage, &dwBytesWritten, nullptr))
    {
        CloseHandle(hFile);
        return E_FAIL;
    }    

    // Close the file
    CloseHandle(hFile);
    return S_OK;
}

HRESULT CCoordinateMappingBasics::LoadResourceImage(
    PCWSTR resourceName,
    PCWSTR resourceType,
    UINT nOutputWidth,
    UINT nOutputHeight,
    RGBQUAD* pOutputBuffer)
{

    HRSRC imageResHandle;
    HGLOBAL imageResDataHandle;

    void* pImageFile = nullptr;
    DWORD imageFileSize = 0;

    Microsoft::WRL::ComPtr<IWICImagingFactory> pIWICFactory;
    V_RET(CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,
        (LPVOID*)&pIWICFactory));

    // Locate the resource
    imageResHandle = FindResourceW(HINST_THISCOMPONENT, resourceName, resourceType);
    V_CHECK_HR(imageResHandle);

    // Load the resource
    imageResDataHandle = LoadResource(HINST_THISCOMPONENT, imageResHandle);
    V_CHECK_HR(imageResDataHandle);

    // Lock it to get a system memory pointer.
    pImageFile = LockResource(imageResDataHandle);
    V_CHECK_HR(pImageFile);

    // Calculate the size.
    imageFileSize = SizeofResource(HINST_THISCOMPONENT, imageResHandle);
    V_CHECK_HR(imageFileSize);

    // Create a WIC stream to map onto the memory.
    Microsoft::WRL::ComPtr<IWICStream> pStream;
    V_RET(pIWICFactory->CreateStream(&pStream));

    // Initialize the stream with the memory pointer and size.
    V_RET(pStream->InitializeFromMemory(
        reinterpret_cast<BYTE*>(pImageFile),
        imageFileSize));

    // Create a decoder for the stream.
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
    V_RET(pIWICFactory->CreateDecoderFromStream(
        pStream.Get(),
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &pDecoder));

    // Create the initial frame.
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pSource;
    V_RET(pDecoder->GetFrame(0, &pSource));

    // Convert the image format to 32bppPBGRA
    // (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
    V_RET(pIWICFactory->CreateFormatConverter(&pConverter));

    Microsoft::WRL::ComPtr<IWICBitmapScaler> pScaler;
    V_RET(pIWICFactory->CreateBitmapScaler(&pScaler));
    V_RET(pScaler->Initialize(
        pSource.Get(),
        nOutputWidth,
        nOutputHeight,
        WICBitmapInterpolationModeCubic));

    V_RET(pConverter->Initialize(
        pScaler.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.f,
        WICBitmapPaletteTypeMedianCut));

    UINT width = 0;
    UINT height = 0;
    V_RET(pConverter->GetSize(&width, &height));

    // make sure the image scaled correctly so the output buffer is big enough
    V_CHECK_HR(!(width != nOutputWidth) || (height != nOutputHeight));

    V_RET(pConverter->CopyPixels(
        nullptr,
        width * sizeof(RGBQUAD),
        nOutputWidth * nOutputHeight * sizeof(RGBQUAD),
        reinterpret_cast<BYTE*>(pOutputBuffer)));

    return S_OK;
}