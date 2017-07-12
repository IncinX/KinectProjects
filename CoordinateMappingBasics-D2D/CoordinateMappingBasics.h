//------------------------------------------------------------------------------
// <copyright file="CoordinateMappingBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma once

#include "resource.h"
#include <memory>
#include "WindowsHelper.h"
#include "ImageRenderer.h"

class CCoordinateMappingBasics
{
    static const int        cDepthWidth  = 512;
    static const int        cDepthHeight = 424;
    static const int        cColorWidth  = 1920;
    static const int        cColorHeight = 1080;

public:
    CCoordinateMappingBasics();
    ~CCoordinateMappingBasics();

    // TODO: Move this stuff to new file
    static LRESULT CALLBACK MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    LRESULT CALLBACK DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    HWND m_hWnd;
    int64_t m_nStartTime;
    int64_t m_nLastCounter;
    double m_fFreq;
    int64_t m_nNextStatusTime;
    DWORD m_nFramesSinceUpdate;
    bool m_bSaveScreenshot;

    // Current Kinect
    Microsoft::WRL::ComPtr<IKinectSensor> m_pKinectSensor;
    Microsoft::WRL::ComPtr<ICoordinateMapper> m_pCoordinateMapper;
    std::unique_ptr<DepthSpacePoint[]> m_pDepthCoordinates;

    // Frame reader
    Microsoft::WRL::ComPtr<IMultiSourceFrameReader> m_pMultiSourceFrameReader;

    // Direct2D
    Microsoft::WRL::ComPtr<ID2D1Factory> m_pD2DFactory;
    std::unique_ptr<ImageRenderer> m_pDrawCoordinateMapping;
    std::unique_ptr<RGBQUAD[]> m_pOutputRGBX; 
    std::unique_ptr<RGBQUAD[]> m_pBackgroundRGBX;
    std::unique_ptr<RGBQUAD[]> m_pColorRGBX;

    void Update();
    HRESULT InitializeDefaultSensor();
    void ProcessFrame(
        int64_t nTime, 
        const UINT16* pDepthBuffer,
        int nDepthHeight,
        int nDepthWidth, 
        const RGBQUAD* pColorBuffer,
        int nColorWidth,
        int nColorHeight,
        const BYTE* pBodyIndexBuffer,
        int nBodyIndexWidth,
        int nBodyIndexHeight);

    bool SetStatusMessage(
        _In_z_ WCHAR* szMessage,
        DWORD nShowTimeMsec,
        bool bForce);

    HRESULT GetScreenshotFileName(
        _Out_writes_z_(nFilePathSize) LPWSTR lpszFilePath,
        UINT nFilePathSize);

    HRESULT SaveBitmapToFile(
        BYTE* pBitmapBits,
        LONG lWidth,
        LONG lHeight,
        WORD wBitsPerPixel,
        LPCWSTR lpszFilePath);

    HRESULT LoadResourceImage(
        PCWSTR resourceName,
        PCWSTR resourceType,
        UINT nOutputWidth,
        UINT nOutputHeight,
        RGBQUAD* pOutputBuffer);
};
