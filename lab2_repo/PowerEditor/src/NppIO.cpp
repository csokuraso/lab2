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
#include "Notepad_plus_Window.h"
#include "FileDialog.h"
#include "EncodingMapper.h"



BufferID Notepad_plus::doOpen(const TCHAR *fileName, bool isReadOnly, int encoding)
{
	NppParameters *pNppParam = NppParameters::getInstance();
	TCHAR longFileName[MAX_PATH];

	::GetFullPathName(fileName, MAX_PATH, longFileName, NULL);
	::GetLongPathName(longFileName, longFileName, MAX_PATH);

	_lastRecentFileList.remove(longFileName);

	const TCHAR * fileName2Find;
	generic_string gs_fileName = fileName;
	size_t res = gs_fileName.find_first_of(UNTITLED_STR);
	 
	if (res != string::npos && res == 0)
	{
		fileName2Find = fileName;
	}
	else
	{
		fileName2Find = longFileName;
	}

	BufferID test = MainFileManager->getBufferFromName(fileName2Find);
	if (test != BUFFER_INVALID)
	{
		//switchToFile(test);
		//Dont switch, not responsibility of doOpen, but of caller
		if (_pTrayIco)
		{
			if (_pTrayIco->isInTray())
			{
				::ShowWindow(_pPublicInterface->getHSelf(), SW_SHOW);
				if (!_pPublicInterface->isPrelaunch())
					_pTrayIco->doTrayIcon(REMOVE);
				::SendMessage(_pPublicInterface->getHSelf(), WM_SIZE, 0, 0);
			}
		}
		return test;
	}

	if (isFileSession(longFileName) && PathFileExists(longFileName)) 
	{
		fileLoadSession(longFileName);
		return BUFFER_INVALID;
	}

	bool isWow64Off = false;
	if (!PathFileExists(longFileName))
	{
		pNppParam->safeWow64EnableWow64FsRedirection(FALSE);
		isWow64Off = true;
	}

	if (!PathFileExists(longFileName))
	{
		TCHAR str2display[MAX_PATH*2];
		generic_string longFileDir(longFileName);
		PathRemoveFileSpec(longFileDir);

		bool isCreateFileSuccessful = false;
		if (PathFileExists(longFileDir.c_str()))
		{
			wsprintf(str2display, TEXT("%s doesn't exist. Create it?"), longFileName);
			if (::MessageBox(_pPublicInterface->getHSelf(), str2display, TEXT("Create new file"), MB_YESNO) == IDYES)
			{
				bool res = MainFileManager->createEmptyFile(longFileName);
				if (res)
				{
					isCreateFileSuccessful = true;
				}
				else
				{
					wsprintf(str2display, TEXT("Cannot create the file \"%s\""), longFileName);
					::MessageBox(_pPublicInterface->getHSelf(), str2display, TEXT("Create new file"), MB_OK);
				}
			}
		}

		if (!isCreateFileSuccessful)
		{
			if (isWow64Off)
			{
				pNppParam->safeWow64EnableWow64FsRedirection(TRUE);
				isWow64Off = false;
			}
			return BUFFER_INVALID;
		}
	}

	// Notify plugins that current file is about to load
	// Plugins can should use this notification to filter SCN_MODIFIED
	SCNotification scnN;
	scnN.nmhdr.code = NPPN_FILEBEFORELOAD;
	scnN.nmhdr.hwndFrom = _pPublicInterface->getHSelf();
	scnN.nmhdr.idFrom = NULL;
	_pluginsManager.notify(&scnN);

	if (encoding == -1)
	{
		encoding = getHtmlXmlEncoding(longFileName);
	}
	
	BufferID buffer = MainFileManager->loadFile(longFileName, NULL, encoding);

	if (buffer != BUFFER_INVALID)
	{
		_isFileOpening = true;

		Buffer * buf = MainFileManager->getBufferByID(buffer);
		// if file is read only, we set the view read only
		if (isReadOnly)
			buf->setUserReadOnly(true);

		// Notify plugins that current file is about to open
		scnN.nmhdr.code = NPPN_FILEBEFOREOPEN;
		scnN.nmhdr.idFrom = (uptr_t)buffer;
		_pluginsManager.notify(&scnN);
		

		loadBufferIntoView(buffer, currentView());

		if (_pTrayIco)
		{
			if (_pTrayIco->isInTray())
			{
				::ShowWindow(_pPublicInterface->getHSelf(), SW_SHOW);
				if (!_pPublicInterface->isPrelaunch())
					_pTrayIco->doTrayIcon(REMOVE);
				::SendMessage(_pPublicInterface->getHSelf(), WM_SIZE, 0, 0);
			}
		}
		PathRemoveFileSpec(longFileName);
		_linkTriggered = true;
		_isDocModifing = false;
		
		_isFileOpening = false;

		// Notify plugins that current file is just opened
		scnN.nmhdr.code = NPPN_FILEOPENED;
		_pluginsManager.notify(&scnN);
	}
	else
	{
		if (::PathIsDirectory(fileName))
		{
			vector<generic_string> fileNames;
			vector<generic_string> patterns;
			patterns.push_back(TEXT("*.*"));

			generic_string fileNameStr = fileName;
			if (fileName[lstrlen(fileName) - 1] != '\\')
				fileNameStr += TEXT("\\");

			getMatchedFileNames(fileNameStr.c_str(), patterns, fileNames, true, false);
			for (size_t i = 0 ; i < fileNames.size() ; i++)
			{
				doOpen(fileNames[i].c_str());
			}
		}
		else
		{
			generic_string msg = TEXT("Can not open file \"");
			msg += longFileName;
			msg += TEXT("\".");
			::MessageBox(_pPublicInterface->getHSelf(), msg.c_str(), TEXT("ERROR"), MB_OK);
			_isFileOpening = false;

			scnN.nmhdr.code = NPPN_FILELOADFAILED;
			_pluginsManager.notify(&scnN);
		}
	}

	if (isWow64Off)
	{
		pNppParam->safeWow64EnableWow64FsRedirection(TRUE);
		isWow64Off = false;
	}
	return buffer;;
}

bool Notepad_plus::doReload(BufferID id, bool alert)
{
	if (alert)
	{
		int answer = _nativeLangSpeaker.messageBox("DocReloadWarning",
			_pPublicInterface->getHSelf(),
			TEXT("Are you sure you want to reload the current file and lose the changes made in Notepad++?"),
			TEXT("Reload"), 
			MB_YESNO | MB_ICONEXCLAMATION | MB_APPLMODAL);
		if (answer != IDYES)
			return false;
	}

	//In order to prevent Scintilla from restyling the entire document,
	//an empty Document is inserted during reload if needed.
	bool mainVisisble = (_mainEditView.getCurrentBufferID() == id);
	bool subVisisble = (_subEditView.getCurrentBufferID() == id);
	if (mainVisisble) {
		_mainEditView.saveCurrentPos();
		_mainEditView.execute(SCI_SETDOCPOINTER, 0, 0);
	}
	if (subVisisble) {
		_subEditView.saveCurrentPos();
		_subEditView.execute(SCI_SETDOCPOINTER, 0, 0);
	}

	if (!mainVisisble && !subVisisble) {
		return MainFileManager->reloadBufferDeferred(id);
	}

	bool res = MainFileManager->reloadBuffer(id);
	Buffer * pBuf = MainFileManager->getBufferByID(id);
	if (mainVisisble) {
		_mainEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
		_mainEditView.restoreCurrentPos();
	}
	if (subVisisble) {
		_subEditView.execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
		_subEditView.restoreCurrentPos();
	}
	return res;
}

bool Notepad_plus::doSave(BufferID id, const TCHAR * filename, bool isCopy)
{
	SCNotification scnN;
	// Notify plugins that current file is about to be saved
	if (!isCopy)
	{
		
		scnN.nmhdr.code = NPPN_FILEBEFORESAVE;
		scnN.nmhdr.hwndFrom = _pPublicInterface->getHSelf();
		scnN.nmhdr.idFrom = (uptr_t)id;
		_pluginsManager.notify(&scnN);
	}

	bool res = MainFileManager->saveBuffer(id, filename, isCopy);

	if (!isCopy)
	{
		scnN.nmhdr.code = NPPN_FILESAVED;
		_pluginsManager.notify(&scnN);
	}

	if (!res)
		_nativeLangSpeaker.messageBox("FileLockedWarning",
		_pPublicInterface->getHSelf(),
		TEXT("Please check whether if this file is opened in another program"),
		TEXT("Save failed"), 
		MB_OK);
	return res;
}

void Notepad_plus::doClose(BufferID id, int whichOne) {
	Buffer * buf = MainFileManager->getBufferByID(id);

	// Notify plugins that current file is about to be closed
	SCNotification scnN;
	scnN.nmhdr.code = NPPN_FILEBEFORECLOSE;
	scnN.nmhdr.hwndFrom = _pPublicInterface->getHSelf();
	scnN.nmhdr.idFrom = (uptr_t)id;
	_pluginsManager.notify(&scnN);

	//add to recent files if its an existing file
	if (!buf->isUntitled())
	{
		// if the file doesn't exist, it could be redirected
		// So we turn Wow64 off
		bool isWow64Off = false;
		NppParameters *pNppParam = NppParameters::getInstance();
		if (!PathFileExists(buf->getFullPathName()))
		{
			pNppParam->safeWow64EnableWow64FsRedirection(FALSE);
			isWow64Off = true;
		}

		if (PathFileExists(buf->getFullPathName()))
			_lastRecentFileList.add(buf->getFullPathName());

		// We enable Wow64 system, if it was disabled
		if (isWow64Off)
		{
			pNppParam->safeWow64EnableWow64FsRedirection(TRUE);
			isWow64Off = false;
		}
	}



	int nrDocs = whichOne==MAIN_VIEW?(_mainDocTab.nbItem()):(_subDocTab.nbItem());

	//Do all the works
	removeBufferFromView(id, whichOne);
	if (nrDocs == 1 && canHideView(whichOne))
	{	//close the view if both visible
		hideView(whichOne);
	}

	// Notify plugins that current file is closed
	scnN.nmhdr.code = NPPN_FILECLOSED;
	_pluginsManager.notify(&scnN);

	return;
}

generic_string Notepad_plus::exts2Filters(generic_string exts) const
{
	const TCHAR *extStr = exts.c_str();
	TCHAR aExt[MAX_PATH];
	generic_string filters(TEXT(""));

	int j = 0;
	bool stop = false;
	for (size_t i = 0 ; i < exts.length() ; i++)
	{
		if (extStr[i] == ' ')
		{
			if (!stop)
			{
				aExt[j] = '\0';
				stop = true;

				if (aExt[0])
				{
					filters += TEXT("*.");
					filters += aExt;
					filters += TEXT(";");
				}
				j = 0;
			}
		}
		else
		{
			aExt[j] = extStr[i];
			stop = false;
			j++;
		}
	}

	if (j > 0)
	{
		aExt[j] = '\0';
		if (aExt[0])
		{
			filters += TEXT("*.");
			filters += aExt;
			filters += TEXT(";");
		}
	}

	// remove the last ';'
    filters = filters.substr(0, filters.length()-1);
	return filters;
}

int Notepad_plus::setFileOpenSaveDlgFilters(FileDialog & fDlg, int langType)
{
	NppParameters *pNppParam = NppParameters::getInstance();
	NppGUI & nppGUI = (NppGUI & )pNppParam->getNppGUI();

	int i = 0;
	Lang *l = NppParameters::getInstance()->getLangFromIndex(i++);

    int ltIndex = 0;
    bool ltFound = false;
	while (l)
	{
		LangType lid = l->getLangID();

		bool inExcludedList = false;
		
		for (size_t j = 0 ; j < nppGUI._excludedLangList.size() ; j++)
		{
			if (lid == nppGUI._excludedLangList[j]._langType)
			{
				inExcludedList = true;
				break;
			}
		}

		if (!inExcludedList)
		{
			const TCHAR *defList = l->getDefaultExtList();
			const TCHAR *userList = NULL;

			LexerStylerArray &lsa = (NppParameters::getInstance())->getLStylerArray();
			const TCHAR *lName = l->getLangName();
			LexerStyler *pLS = lsa.getLexerStylerByName(lName);
			
			if (pLS)
				userList = pLS->getLexerUserExt();

			generic_string list(TEXT(""));
			if (defList)
				list += defList;
			if (userList)
			{
				list += TEXT(" ");
				list += userList;
			}
			
			generic_string stringFilters = exts2Filters(list);
			const TCHAR *filters = stringFilters.c_str();
			if (filters[0])
			{
				fDlg.setExtsFilter(getLangDesc(lid, true).c_str(), filters);

                //
                // Get index of lang type to find
                //
                if (langType != -1 && !ltFound)
                {
                    ltFound = langType == lid;
                }

                if (langType != -1 && !ltFound)
                {
                    ltIndex++;
                }
			}
		}
		l = (NppParameters::getInstance())->getLangFromIndex(i++);
	}

    if (!ltFound)
        return -1;
    return ltIndex;
}


bool Notepad_plus::fileClose(BufferID id, int curView)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	Buffer * buf = MainFileManager->getBufferByID(bufferID);

	int res;

	//process the fileNamePath into LRF
	const TCHAR *fileNamePath = buf->getFullPathName();

	if (buf->isUntitled() && buf->docLength() == 0)
	{
		// Do nothing
	}
	else if (buf->isDirty())
	{
		
		res = doSaveOrNot(fileNamePath);
		if (res == IDYES)
		{
			if (!fileSave(id)) // the cancel button of savedialog is pressed, aborts closing
				return false;
		}
		else if (res == IDCANCEL)
		{
			return false;	//cancel aborts closing
		}
		else
		{
			// else IDNO we continue
		}
	}

	int viewToClose = currentView();
	if (curView != -1)
		viewToClose = curView;
	//first check amount of documents, we dont want the view to hide if we closed a secondary doc with primary being empty
	//int nrDocs = _pDocTab->nbItem();
	doClose(bufferID, viewToClose);
	return true;
}

bool Notepad_plus::fileCloseAll()
{
	//closes all documents, makes the current view the only one visible

	//first check if we need to save any file
	for(int i = 0; i < _mainDocTab.nbItem(); i++)
	{
		BufferID id = _mainDocTab.getBufferByIndex(i);
		Buffer * buf = MainFileManager->getBufferByID(id);
		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty()) 
		{
			_mainDocTab.activateBuffer(id);
			_mainEditView.activateBuffer(id);

			int res = doSaveOrNot(buf->getFullPathName());
			if (res == IDYES) 
			{
				if (!fileSave(id))
					return false;	//abort entire procedure
			} 
			else if (res == IDCANCEL) 
			{
					return false;
			}
		}
	}
	for(int i = 0; i < _subDocTab.nbItem(); i++) 
	{
		BufferID id = _subDocTab.getBufferByIndex(i);
		Buffer * buf = MainFileManager->getBufferByID(id);
		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty())
		{
			_subDocTab.activateBuffer(id);
			_subEditView.activateBuffer(id);

			int res = doSaveOrNot(buf->getFullPathName());
			if (res == IDYES)
			{
				if (!fileSave(id))
					return false;	//abort entire procedure
			}
			else if (res == IDCANCEL) 
			{
					return false;
				//otherwise continue (IDNO)
			}
		}
	}

	//Then start closing, inactive view first so the active is left open
    if (bothActive())
    {	//first close all docs in non-current view, which gets closed automatically
		//Set active tab to the last one closed.
		activateBuffer(_pNonDocTab->getBufferByIndex(0), otherView());
		for(int i = _pNonDocTab->nbItem() - 1; i >= 0; i--) {	//close all from right to left
			doClose(_pNonDocTab->getBufferByIndex(i), otherView());
		}
		//hideView(otherView());
    }

	activateBuffer(_pDocTab->getBufferByIndex(0), currentView());
	for(int i = _pDocTab->nbItem() - 1; i >= 0; i--) {	//close all from right to left
		doClose(_pDocTab->getBufferByIndex(i), currentView());
	}
	return true;
}

bool Notepad_plus::fileCloseAllButCurrent()
{
	BufferID current = _pEditView->getCurrentBufferID();
	int active = _pDocTab->getCurrentTabIndex();
	//closes all documents, makes the current view the only one visible

	//first check if we need to save any file
	for(int i = 0; i < _mainDocTab.nbItem(); i++) {
		BufferID id = _mainDocTab.getBufferByIndex(i);
		if (id == current)
			continue;
		Buffer * buf = MainFileManager->getBufferByID(id);
		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty()) 
		{
			_mainDocTab.activateBuffer(id);
			_mainEditView.activateBuffer(id);

			int res = doSaveOrNot(buf->getFullPathName());
			if (res == IDYES) 
			{
				if (!fileSave(id))
					return false;	//abort entire procedure
			} 
			else if (res == IDCANCEL)
			{
					return false;
			}
		}
	}
	for(int i = 0; i < _subDocTab.nbItem(); i++) 
	{
		BufferID id = _subDocTab.getBufferByIndex(i);
		Buffer * buf = MainFileManager->getBufferByID(id);
		if (id == current)
			continue;
		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty()) 
		{
			_subDocTab.activateBuffer(id);
			_subEditView.activateBuffer(id);

			int res = doSaveOrNot(buf->getFullPathName());
			if (res == IDYES) 
			{
				if (!fileSave(id))
					return false;	//abort entire procedure
			} 
			else if (res == IDCANCEL) 
			{
					return false;
			}
		}
	}

	//Then start closing, inactive view first so the active is left open
    if (bothActive())
    {	//first close all docs in non-current view, which gets closed automatically
		//Set active tab to the last one closed.
		activateBuffer(_pNonDocTab->getBufferByIndex(0), otherView());
		for(int i = _pNonDocTab->nbItem() - 1; i >= 0; i--) {	//close all from right to left
			doClose(_pNonDocTab->getBufferByIndex(i), otherView());
		}
		//hideView(otherView());
    }

	activateBuffer(_pDocTab->getBufferByIndex(0), currentView());
	for(int i = _pDocTab->nbItem() - 1; i >= 0; i--) {	//close all from right to left
		if (i == active) {	//dont close active index
			continue;
		}
		doClose(_pDocTab->getBufferByIndex(i), currentView());
	}
	return true;
}

bool Notepad_plus::fileSave(BufferID id)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	Buffer * buf = MainFileManager->getBufferByID(bufferID);

	if (!buf->getFileReadOnly() && buf->isDirty())	//cannot save if readonly
	{
		const TCHAR *fn = buf->getFullPathName();
		if (buf->isUntitled())
		{
			return fileSaveAs(bufferID);
		}
		else
		{
			const NppGUI & nppgui = (NppParameters::getInstance())->getNppGUI();
			BackupFeature backup = nppgui._backup;
			TCHAR *name = ::PathFindFileName(fn);

			if (backup == bak_simple)
			{
				//copy fn to fn.backup
				generic_string fn_bak(fn);
				if ((nppgui._useDir) && (nppgui._backupDir != TEXT("")))
				{
					fn_bak = nppgui._backupDir;
					fn_bak += TEXT("\\");
					fn_bak += name;
				}
				else
				{
					fn_bak = fn;
				}
				fn_bak += TEXT(".bak");
				::CopyFile(fn, fn_bak.c_str(), FALSE);
			}
			else if (backup == bak_verbose)
			{
				generic_string fn_dateTime_bak(TEXT(""));
				
				if ((nppgui._useDir) && (nppgui._backupDir != TEXT("")))
				{
					fn_dateTime_bak = nppgui._backupDir;
					fn_dateTime_bak += TEXT("\\");
				}
				else
				{
					const TCHAR *bakDir = TEXT("nppBackup");

					// std::string path should be a temp throwable variable 
					generic_string path = fn;
					::PathRemoveFileSpec(path);
					fn_dateTime_bak = path.c_str();
					

					fn_dateTime_bak += TEXT("\\");
					fn_dateTime_bak += bakDir;
					fn_dateTime_bak += TEXT("\\");

					if (!::PathFileExists(fn_dateTime_bak.c_str()))
					{
						::CreateDirectory(fn_dateTime_bak.c_str(), NULL);
					}
				}

				fn_dateTime_bak += name;

				const int temBufLen = 32;
				TCHAR tmpbuf[temBufLen];
				time_t ltime = time(0);
				struct tm *today;

				today = localtime(&ltime);
				generic_strftime(tmpbuf, temBufLen, TEXT("%Y-%m-%d_%H%M%S"), today);

				fn_dateTime_bak += TEXT(".");
				fn_dateTime_bak += tmpbuf;
				fn_dateTime_bak += TEXT(".bak");

				::CopyFile(fn, fn_dateTime_bak.c_str(), FALSE);
			}
			return doSave(bufferID, buf->getFullPathName(), false);
		}
	}
	return false;
}

bool Notepad_plus::fileSaveAll() {
	if (viewVisible(MAIN_VIEW)) {
		for(int i = 0; i < _mainDocTab.nbItem(); i++) {
			BufferID idToSave = _mainDocTab.getBufferByIndex(i);
			fileSave(idToSave);
		}
	}

	if (viewVisible(SUB_VIEW)) {
		for(int i = 0; i < _subDocTab.nbItem(); i++) {
			BufferID idToSave = _subDocTab.getBufferByIndex(i);
			fileSave(idToSave);
		}
	}
	return true;
}

bool Notepad_plus::fileSaveAs(BufferID id, bool isSaveCopy)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	Buffer * buf = MainFileManager->getBufferByID(bufferID);

	FileDialog fDlg(_pPublicInterface->getHSelf(), _pPublicInterface->getHinst());

    fDlg.setExtFilter(TEXT("All types"), TEXT(".*"), NULL);
	int langTypeIndex = setFileOpenSaveDlgFilters(fDlg, buf->getLangType());
	fDlg.setDefFileName(buf->getFileName());
	
    fDlg.setExtIndex(langTypeIndex+1); // +1 for "All types"
	TCHAR *pfn = fDlg.doSaveDlg();

	if (pfn)
	{
		BufferID other = _pNonDocTab->findBufferByName(pfn);
		if (other == BUFFER_INVALID)	//can save, other view doesnt contain buffer
		{
			bool res = doSave(bufferID, pfn, isSaveCopy);
			//buf->setNeedsLexing(true);	//commented to fix wrapping being removed after save as (due to SCI_CLEARSTYLE or something, seems to be Scintilla bug)
			//Changing lexer after save seems to work properly
			return res;
		}
		else		//cannot save, other view has buffer already open, activate it
		{
			_nativeLangSpeaker.messageBox("FileAlreadyOpenedInNpp",
				_pPublicInterface->getHSelf(),
				TEXT("The file is already opened in the Notepad++."),
				TEXT("ERROR"),
				MB_OK | MB_ICONSTOP);
			switchToFile(other);
			return false;
		}
	}
	else // cancel button is pressed
    {
        checkModifiedDocument();
		return false;
    }
}

bool Notepad_plus::fileRename(BufferID id)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	Buffer * buf = MainFileManager->getBufferByID(bufferID);

	FileDialog fDlg(_pPublicInterface->getHSelf(), _pPublicInterface->getHinst());

    fDlg.setExtFilter(TEXT("All types"), TEXT(".*"), NULL);
	setFileOpenSaveDlgFilters(fDlg);
			
	fDlg.setDefFileName(buf->getFileName());
	TCHAR *pfn = fDlg.doSaveDlg();

	if (pfn)
	{
		MainFileManager->moveFile(bufferID, pfn);
	}
	return false;
}


bool Notepad_plus::fileDelete(BufferID id)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	
	Buffer * buf = MainFileManager->getBufferByID(bufferID);
	const TCHAR *fileNamePath = buf->getFullPathName();

	if (doDeleteOrNot(fileNamePath) == IDYES)
	{
		if (!MainFileManager->deleteFile(bufferID))
		{
			_nativeLangSpeaker.messageBox("DeleteFileFailed",
				_pPublicInterface->getHSelf(),
				TEXT("Delete File failed"),
				TEXT("Delete File"),
				MB_OK);
			return false;
		}
		doClose(bufferID, MAIN_VIEW);
		doClose(bufferID, SUB_VIEW);
		return true;
	}
	return false;
}

void Notepad_plus::fileOpen()
{
    FileDialog fDlg(_pPublicInterface->getHSelf(), _pPublicInterface->getHinst());
	fDlg.setExtFilter(TEXT("All types"), TEXT(".*"), NULL);
	
	setFileOpenSaveDlgFilters(fDlg);

	BufferID lastOpened = BUFFER_INVALID;
	if (stringVector *pfns = fDlg.doOpenMultiFilesDlg())
	{
		size_t sz = pfns->size();
		for (size_t i = 0 ; i < sz ; i++) {
			BufferID test = doOpen(pfns->at(i).c_str(), fDlg.isReadOnly());
			if (test != BUFFER_INVALID)
				lastOpened = test;
		}
	}
	if (lastOpened != BUFFER_INVALID) {
		switchToFile(lastOpened);
	}
}

void Notepad_plus::fileNew()
{
    BufferID newBufID = MainFileManager->newEmptyDocument();
    loadBufferIntoView(newBufID, currentView(), true);	//true, because we want multiple new files if possible
    activateBuffer(newBufID, currentView());
}

bool Notepad_plus::isFileSession(const TCHAR * filename) {
	// if file2open matches the ext of user defined session file ext, then it'll be opened as a session
	const TCHAR *definedSessionExt = NppParameters::getInstance()->getNppGUI()._definedSessionExt.c_str();
	if (*definedSessionExt != '\0')
	{
		generic_string fncp = filename;
		TCHAR *pExt = PathFindExtension(fncp.c_str());

		generic_string usrSessionExt = TEXT("");
		if (*definedSessionExt != '.')
		{
			usrSessionExt += TEXT(".");
		}
		usrSessionExt += definedSessionExt;

		if (!generic_stricmp(pExt, usrSessionExt.c_str()))
		{
			return true;
		}
	}
	return false;
}


// return true if all the session files are loaded
// return false if one or more sessions files fail to load (and session is modify to remove invalid files)
bool Notepad_plus::loadSession(Session & session)
{
	NppParameters *pNppParam = NppParameters::getInstance();
	bool allSessionFilesLoaded = true;
	BufferID lastOpened = BUFFER_INVALID;
	size_t i = 0;
	showView(MAIN_VIEW);
	switchEditViewTo(MAIN_VIEW);	//open files in main
	for ( ; i < session.nbMainFiles() ; )
	{
		const TCHAR *pFn = session._mainViewFiles[i]._fileName.c_str();
		if (isFileSession(pFn))
		{
			vector<sessionFileInfo>::iterator posIt = session._mainViewFiles.begin() + i;
			session._mainViewFiles.erase(posIt);
			continue;	//skip session files, not supporting recursive sessions
		}

		bool isWow64Off = false;
		if (!PathFileExists(pFn))
		{
			pNppParam->safeWow64EnableWow64FsRedirection(FALSE);
			isWow64Off = true;
		}
		if (PathFileExists(pFn)) 
		{
			lastOpened = doOpen(pFn, false, session._mainViewFiles[i]._encoding);
		}
		else
		{
			lastOpened = BUFFER_INVALID;
		}
		if (isWow64Off)
		{
			pNppParam->safeWow64EnableWow64FsRedirection(TRUE);
			isWow64Off = false;
		}

		if (lastOpened != BUFFER_INVALID)
		{
			showView(MAIN_VIEW);
			const TCHAR *pLn = session._mainViewFiles[i]._langName.c_str();
			int id = getLangFromMenuName(pLn);
			LangType typeToSet = L_TEXT;
			if (id != 0 && lstrcmp(pLn, TEXT("User Defined")) != 0)
				typeToSet = menuID2LangType(id);
			if (typeToSet == L_EXTERNAL )
				typeToSet = (LangType)(id - IDM_LANG_EXTERNAL + L_EXTERNAL);

			Buffer * buf = MainFileManager->getBufferByID(lastOpened);
			buf->setPosition(session._mainViewFiles[i], &_mainEditView);
			buf->setLangType(typeToSet, pLn);
			if (session._mainViewFiles[i]._encoding != -1)
				buf->setEncoding(session._mainViewFiles[i]._encoding);

			//Force in the document so we can add the markers
			//Dont use default methods because of performance
			Document prevDoc = _mainEditView.execute(SCI_GETDOCPOINTER);
			_mainEditView.execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			for (size_t j = 0 ; j < session._mainViewFiles[i].marks.size() ; j++) 
			{
				_mainEditView.execute(SCI_MARKERADD, session._mainViewFiles[i].marks[j], MARK_BOOKMARK);
			}
			_mainEditView.execute(SCI_SETDOCPOINTER, 0, prevDoc);
			i++;
		}
		else
		{
			vector<sessionFileInfo>::iterator posIt = session._mainViewFiles.begin() + i;
			session._mainViewFiles.erase(posIt);
			allSessionFilesLoaded = false;
		}
	}

	size_t k = 0;
	showView(SUB_VIEW);
	switchEditViewTo(SUB_VIEW);	//open files in sub
	for ( ; k < session.nbSubFiles() ; )
	{
		const TCHAR *pFn = session._subViewFiles[k]._fileName.c_str();
		if (isFileSession(pFn)) {
			vector<sessionFileInfo>::iterator posIt = session._subViewFiles.begin() + k;
			session._subViewFiles.erase(posIt);
			continue;	//skip session files, not supporting recursive sessions
		}

		bool isWow64Off = false;
		if (!PathFileExists(pFn))
		{
			pNppParam->safeWow64EnableWow64FsRedirection(FALSE);
			isWow64Off = true;
		}
		if (PathFileExists(pFn)) 
		{
			lastOpened = doOpen(pFn, false, session._subViewFiles[k]._encoding);
			//check if already open in main. If so, clone
			if (_mainDocTab.getIndexByBuffer(lastOpened) != -1) {
				loadBufferIntoView(lastOpened, SUB_VIEW);
			}
		}
		else 
		{
			lastOpened = BUFFER_INVALID;
		}
		if (isWow64Off)
		{
			pNppParam->safeWow64EnableWow64FsRedirection(TRUE);
			isWow64Off = false;
		}

		if (lastOpened != BUFFER_INVALID)
		{
			showView(SUB_VIEW);
			if (canHideView(MAIN_VIEW))
				hideView(MAIN_VIEW);
			const TCHAR *pLn = session._subViewFiles[k]._langName.c_str();
			int id = getLangFromMenuName(pLn);
			LangType typeToSet = L_TEXT;
			if (id != 0)
				typeToSet = menuID2LangType(id);
			if (typeToSet == L_EXTERNAL )
				typeToSet = (LangType)(id - IDM_LANG_EXTERNAL + L_EXTERNAL);

			Buffer * buf = MainFileManager->getBufferByID(lastOpened);
			buf->setPosition(session._subViewFiles[k], &_subEditView);
			if (typeToSet == L_USER) {
				if (!lstrcmp(pLn, TEXT("User Defined"))) {
					pLn = TEXT("");	//default user defined
				}
			}
			buf->setLangType(typeToSet, pLn);
			buf->setEncoding(session._subViewFiles[k]._encoding);
			
			//Force in the document so we can add the markers
			//Dont use default methods because of performance
			Document prevDoc = _subEditView.execute(SCI_GETDOCPOINTER);
			_subEditView.execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			for (size_t j = 0 ; j < session._subViewFiles[k].marks.size() ; j++) 
			{
				_subEditView.execute(SCI_MARKERADD, session._subViewFiles[k].marks[j], MARK_BOOKMARK);
			}
			_subEditView.execute(SCI_SETDOCPOINTER, 0, prevDoc);

			k++;
		}
		else
		{
			vector<sessionFileInfo>::iterator posIt = session._subViewFiles.begin() + k;
			session._subViewFiles.erase(posIt);
			allSessionFilesLoaded = false;
		}
	}

	_mainEditView.restoreCurrentPos();
	_subEditView.restoreCurrentPos();

	if (session._activeMainIndex < (size_t)_mainDocTab.nbItem())//session.nbMainFiles())
		activateBuffer(_mainDocTab.getBufferByIndex(session._activeMainIndex), MAIN_VIEW);

	if (session._activeSubIndex < (size_t)_subDocTab.nbItem())//session.nbSubFiles())
		activateBuffer(_subDocTab.getBufferByIndex(session._activeSubIndex), SUB_VIEW);

	if ((session.nbSubFiles() > 0) && (session._activeView == MAIN_VIEW || session._activeView == SUB_VIEW))
		switchEditViewTo(session._activeView);
	else
		switchEditViewTo(MAIN_VIEW);

	if (canHideView(otherView()))
		hideView(otherView());
	else if (canHideView(currentView()))
		hideView(currentView());
	return allSessionFilesLoaded;
}


bool Notepad_plus::fileLoadSession(const TCHAR *fn)
{
	bool result = false;
	const TCHAR *sessionFileName = NULL;
	if (fn == NULL)
	{
		FileDialog fDlg(_pPublicInterface->getHSelf(), _pPublicInterface->getHinst());
		fDlg.setExtFilter(TEXT("All types"), TEXT(".*"), NULL);
		const TCHAR *ext = NppParameters::getInstance()->getNppGUI()._definedSessionExt.c_str();
		generic_string sessionExt = TEXT("");
		if (*ext != '\0')
		{
			if (*ext != '.') 
				sessionExt += TEXT(".");
			sessionExt += ext;
			fDlg.setExtFilter(TEXT("Session file"), sessionExt.c_str(), NULL);
		}
		sessionFileName = fDlg.doOpenSingleFileDlg();
	}
	else
	{
		if (PathFileExists(fn))
			sessionFileName = fn;
	}
	
	if (sessionFileName)
	{
		bool isAllSuccessful = true;
		Session session2Load;

		if ((NppParameters::getInstance())->loadSession(session2Load, sessionFileName))
		{
			isAllSuccessful = loadSession(session2Load);
			result = true;
		}
		if (!isAllSuccessful)
			(NppParameters::getInstance())->writeSession(session2Load, sessionFileName);
	}
	return result;
}

const TCHAR * Notepad_plus::fileSaveSession(size_t nbFile, TCHAR ** fileNames, const TCHAR *sessionFile2save)
{
	if (sessionFile2save)
	{
		Session currentSession;
		if ((nbFile) && (fileNames))
		{
			for (size_t i = 0 ; i < nbFile ; i++)
			{
				if (PathFileExists(fileNames[i]))
					currentSession._mainViewFiles.push_back(generic_string(fileNames[i]));
			}
		}
		else
			getCurrentOpenedFiles(currentSession);

		(NppParameters::getInstance())->writeSession(currentSession, sessionFile2save);
		return sessionFile2save;
	}
	return NULL;
}

const TCHAR * Notepad_plus::fileSaveSession(size_t nbFile, TCHAR ** fileNames)
{
	const TCHAR *sessionFileName = NULL;
	
	FileDialog fDlg(_pPublicInterface->getHSelf(), _pPublicInterface->getHinst());
	const TCHAR *ext = NppParameters::getInstance()->getNppGUI()._definedSessionExt.c_str();

	fDlg.setExtFilter(TEXT("All types"), TEXT(".*"), NULL);
	generic_string sessionExt = TEXT("");
	if (*ext != '\0')
	{
		if (*ext != '.') 
			sessionExt += TEXT(".");
		sessionExt += ext;
		fDlg.setExtFilter(TEXT("Session file"), sessionExt.c_str(), NULL);
	}
	sessionFileName = fDlg.doSaveDlg();

	return fileSaveSession(nbFile, fileNames, sessionFileName);
}


void Notepad_plus::saveSession(const Session & session)
{
	(NppParameters::getInstance())->writeSession(session);
}
