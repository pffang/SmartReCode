// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "MainDlg.h"
#include "aboutdlg.h"

BOOL CMainDlg::PreTranslateMessage(MSG *pMsg) { return CWindow::IsDialogMessage(pMsg); }

BOOL CMainDlg::OnIdle() {
    UIUpdateChildWindows();
    return FALSE;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & /*bHandled*/) {
    // center the dialog on the screen
    CenterWindow();

    // set icons
    HICON hIcon =
        AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
    SetIcon(hIcon, TRUE);
    HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON),
                                        ::GetSystemMetrics(SM_CYSMICON));
    SetIcon(hIconSmall, FALSE);

    // register object for message filtering and idle updates
    CMessageLoop *pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);

    DragAcceptFiles(TRUE);

    /* Get Menu Handle to top window */
    HMENU hSysMenu = GetSystemMenu(FALSE);
    if (hSysMenu != INVALID_HANDLE_VALUE) {
        /* Append separator menu item */
        AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
        /* Append string type menu */
        AppendMenu(hSysMenu, MF_STRING, ID_APP_ABOUT, _T("About..."));
    }

    UIAddChildWindowContainer(m_hWnd);

    return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL & /*bHandled*/) {
    // unregister message filtering and idle updates
    CMessageLoop *pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->RemoveMessageFilter(this);
    pLoop->RemoveIdleHandler(this);

    return 0;
}

LRESULT CMainDlg::OnSysCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled) {
    if (wParam == ID_APP_ABOUT) {
        CAboutDlg dlg;
        dlg.DoModal();
        bHandled = TRUE;
    } else {
        bHandled = FALSE;
    }

    return 0;
}

LRESULT CMainDlg::OnDropFiles(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled) {
    HDROP hDropInfo = (HDROP)wParam;
    UINT  filePathesCount = DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0);
     for (UINT i = 0; i < filePathesCount; ++i) {
         CString fileName;
         UINT    fileNameLength = DragQueryFile(hDropInfo, i, NULL, 0);
         DragQueryFile(hDropInfo, i, fileName.GetBuffer(fileNameLength + 1), fileNameLength + 1);
         fileName.ReleaseBuffer();
         DWORD dwAttribute = GetFileAttributes(fileName);

         if (dwAttribute & FILE_ATTRIBUTE_DIRECTORY) {
             SetCurrentDirectory(fileName);
             //EnumerateFiles();
         } else {
             OutputDebugString(fileName);
             //MessageBox(fileName, _T("Debug"), MB_OK);
         }
    }
    DragFinish(hDropInfo);

    return 0;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
    // TODO: Add validation code
    CloseDialog(wID);
    return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL & /*bHandled*/) {
    CloseDialog(wID);
    return 0;
}

void CMainDlg::CloseDialog(int nVal) {
    DestroyWindow();
    ::PostQuitMessage(nVal);
}
