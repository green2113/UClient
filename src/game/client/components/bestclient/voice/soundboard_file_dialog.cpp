/* Copyright © 2026 BestProject Team */
/* Windows-only file dialog for soundboard file selection.
   Kept in a separate TU to avoid IStorage collision between
   Windows COM (objidl.h) and DDNet's engine/storage.h. */

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#include <string>

bool OpenSoundboardFileDialog(char *pOutPath, int OutPathSize)
{
	if(pOutPath == nullptr || OutPathSize <= 0)
		return false;

	const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool NeedUninit = SUCCEEDED(InitHr);

	IFileOpenDialog *pDialog = nullptr;
	HRESULT Hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDialog));
	if(FAILED(Hr) || pDialog == nullptr)
	{
		if(NeedUninit) CoUninitialize();
		return false;
	}

	DWORD Options = 0;
	if(SUCCEEDED(pDialog->GetOptions(&Options)))
		pDialog->SetOptions(Options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);

	const COMDLG_FILTERSPEC aFilterSpecs[] = {
		{L"Audio files (*.wav;*.mp3;*.ogg)", L"*.wav;*.mp3;*.ogg"},
		{L"All files (*.*)", L"*.*"},
	};
	pDialog->SetFileTypes((UINT)std::size(aFilterSpecs), aFilterSpecs);
	pDialog->SetFileTypeIndex(1);
	pDialog->SetTitle(L"Select soundboard audio file");

	Hr = pDialog->Show(nullptr);
	if(FAILED(Hr)) { pDialog->Release(); if(NeedUninit) CoUninitialize(); return false; }

	IShellItem *pResult = nullptr;
	Hr = pDialog->GetResult(&pResult);
	if(FAILED(Hr) || pResult == nullptr) { pDialog->Release(); if(NeedUninit) CoUninitialize(); return false; }

	PWSTR pWidePath = nullptr;
	Hr = pResult->GetDisplayName(SIGDN_FILESYSPATH, &pWidePath);
	bool Converted = false;
	if(SUCCEEDED(Hr) && pWidePath != nullptr)
	{
		int Len = WideCharToMultiByte(CP_UTF8, 0, pWidePath, -1, pOutPath, OutPathSize, nullptr, nullptr);
		Converted = Len > 0;
		CoTaskMemFree(pWidePath);
	}
	pResult->Release();
	pDialog->Release();
	if(NeedUninit) CoUninitialize();
	return Converted;
}

#endif
