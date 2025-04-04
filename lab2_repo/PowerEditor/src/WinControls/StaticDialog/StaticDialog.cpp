//this file is part of notepad++
//Copyright (C)2003 Don HO ( donho@altern.org )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "precompiledHeaders.h"

void StaticDialog::goToCenter()
{
    RECT rc;
    ::GetClientRect(_hParent, &rc);
    POINT center;
    center.x = rc.left + (rc.right - rc.left)/2;
    center.y = rc.top + (rc.bottom - rc.top)/2;
    ::ClientToScreen(_hParent, &center);

	int x = center.x - (_rc.right - _rc.left)/2;
	int y = center.y - (_rc.bottom - _rc.top)/2;

	::SetWindowPos(_hSelf, HWND_TOP, x, y, _rc.right - _rc.left, _rc.bottom - _rc.top, SWP_SHOWWINDOW);
}


void StaticDialog::display(bool toShow) const 
{
	if (toShow) {
		// If the user has switched from a dual monitor to a single monitor since we last
		// displayed the dialog, then ensure that it's still visible on the single monitor.
		RECT workAreaRect, rc;
		::SystemParametersInfo(SPI_GETWORKAREA, 0, &workAreaRect, 0);
		::GetWindowRect(_hSelf, &rc);
		int newLeft = rc.left;
		int newTop = rc.top;
		int margin = ::GetSystemMetrics(SM_CYSMCAPTION);
		if (newLeft > ::GetSystemMetrics(SM_CXVIRTUALSCREEN)-margin)
			newLeft -= rc.right - workAreaRect.right;
		if (newLeft + (rc.right - rc.left) < ::GetSystemMetrics(SM_XVIRTUALSCREEN)+margin)
			newLeft = workAreaRect.left;
		if (newTop > ::GetSystemMetrics(SM_CYVIRTUALSCREEN)-margin)
			newTop -= rc.bottom - workAreaRect.bottom;
		if (newTop + (rc.bottom - rc.top) < ::GetSystemMetrics(SM_YVIRTUALSCREEN)+margin)
			newTop = workAreaRect.top;

		if ((newLeft != rc.left) || (newTop != rc.top)) // then the virtual screen size has shrunk
			// Remember that MoveWindow wants width/height.
			::MoveWindow(_hSelf, newLeft, newTop, rc.right - rc.left, rc.bottom - rc.top, TRUE);
	}

	Window::display(toShow);
}


HGLOBAL StaticDialog::makeRTLResource(int dialogID, DLGTEMPLATE **ppMyDlgTemplate)
{
	// Get Dlg Template resource
	HRSRC  hDialogRC = ::FindResource(_hInst, MAKEINTRESOURCE(dialogID), RT_DIALOG);
	HGLOBAL  hDlgTemplate = ::LoadResource(_hInst, hDialogRC);
	DLGTEMPLATE *pDlgTemplate = (DLGTEMPLATE *)::LockResource(hDlgTemplate);
	
	// Duplicate Dlg Template resource
	unsigned long sizeDlg = ::SizeofResource(_hInst, hDialogRC);
	HGLOBAL hMyDlgTemplate = ::GlobalAlloc(GPTR, sizeDlg);
	*ppMyDlgTemplate = (DLGTEMPLATE *)::GlobalLock(hMyDlgTemplate);

	::memcpy(*ppMyDlgTemplate, pDlgTemplate, sizeDlg);
	
	DLGTEMPLATEEX *pMyDlgTemplateEx = (DLGTEMPLATEEX *)*ppMyDlgTemplate;
	if (pMyDlgTemplateEx->signature == 0xFFFF)
		pMyDlgTemplateEx->exStyle |= WS_EX_LAYOUTRTL;
	else
		(*ppMyDlgTemplate)->dwExtendedStyle |= WS_EX_LAYOUTRTL;

	return hMyDlgTemplate;
}

void StaticDialog::create(int dialogID, bool isRTL, bool msgDestParent)
{
	if (isRTL)
	{
		DLGTEMPLATE *pMyDlgTemplate = NULL;
		HGLOBAL hMyDlgTemplate = makeRTLResource(dialogID, &pMyDlgTemplate);
		_hSelf = ::CreateDialogIndirectParam(_hInst, pMyDlgTemplate, _hParent, (DLGPROC)dlgProc, (LPARAM)this);
		::GlobalFree(hMyDlgTemplate);
	}
	else
		_hSelf = ::CreateDialogParam(_hInst, MAKEINTRESOURCE(dialogID), _hParent, (DLGPROC)dlgProc, (LPARAM)this);

	if (!_hSelf)
	{
		return;
	}

	// if the destination of message NPPM_MODELESSDIALOG is not its parent, then it's the grand-parent
	::SendMessage(msgDestParent?_hParent:(::GetParent(_hParent)), NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (WPARAM)_hSelf);
}

BOOL CALLBACK StaticDialog::dlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) 
{
	switch (message) 
	{
		case WM_INITDIALOG :
		{
			StaticDialog *pStaticDlg = (StaticDialog *)(lParam);
			pStaticDlg->_hSelf = hwnd;
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
			::GetWindowRect(hwnd, &(pStaticDlg->_rc));
            pStaticDlg->run_dlgProc(message, wParam, lParam);
			
			return TRUE;
		}

		default :
		{
			StaticDialog *pStaticDlg = reinterpret_cast<StaticDialog *>(::GetWindowLongPtr(hwnd, GWL_USERDATA));
			if (!pStaticDlg)
				return FALSE;
			return pStaticDlg->run_dlgProc(message, wParam, lParam);
		}
	}
}

void StaticDialog::alignWith(HWND handle, HWND handle2Align, PosAlign pos, POINT & point)
{
    RECT rc, rc2;
    ::GetWindowRect(handle, &rc);

    point.x = rc.left;
    point.y = rc.top;

    switch (pos)
    {
        case ALIGNPOS_LEFT :
            ::GetWindowRect(handle2Align, &rc2);
            point.x -= rc2.right - rc2.left;
            break;

        case ALIGNPOS_RIGHT :
            ::GetWindowRect(handle, &rc2);
            point.x += rc2.right - rc2.left;
            break;

        case ALIGNPOS_TOP :
            ::GetWindowRect(handle2Align, &rc2);
            point.y -= rc2.bottom - rc2.top;
            break;

        default : //ALIGNPOS_BOTTOM
            ::GetWindowRect(handle, &rc2);
            point.y += rc2.bottom - rc2.top;
            break;
    }
    
    ::ScreenToClient(_hSelf, &point);
}
