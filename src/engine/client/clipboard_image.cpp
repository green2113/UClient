#include "clipboard_image.h"

#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifdef NOGDI
#undef NOGDI
#define UC_CLIPBOARD_RESTORE_NOGDI
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <SDL_syswm.h>
#include <SDL_video.h>
#include <engine/gfx/image_loader.h>
#include <engine/image.h>

#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

namespace
{
HWND s_ClipboardOwnerWindow = nullptr;

struct SBitmapInfoHeader
{
	DWORD m_Size;
	LONG m_Width;
	LONG m_Height;
	WORD m_Planes;
	WORD m_BitCount;
	DWORD m_Compression;
	DWORD m_SizeImage;
	LONG m_XPelsPerMeter;
	LONG m_YPelsPerMeter;
	DWORD m_ClrUsed;
	DWORD m_ClrImportant;
};

struct SBitmapV4Header
{
	SBitmapInfoHeader m_Base;
	DWORD m_RedMask;
	DWORD m_GreenMask;
	DWORD m_BlueMask;
	DWORD m_AlphaMask;
	DWORD m_CSType;
	LONG m_Endpoints[9];
	DWORD m_GammaRed;
	DWORD m_GammaGreen;
	DWORD m_GammaBlue;
};

constexpr DWORD DIB_COMPRESSION_RGB = 0;
constexpr DWORD DIB_COMPRESSION_BITFIELDS = 3;

uint8_t ExtractMaskedChannel(uint32_t Pixel, uint32_t Mask)
{
	if(Mask == 0)
		return 0;

	int Shift = 0;
	while(((Mask >> Shift) & 1u) == 0)
		++Shift;

	uint32_t Range = Mask >> Shift;
	int Bits = 0;
	while((Range & 1u) != 0)
	{
		++Bits;
		Range >>= 1;
	}

	if(Bits <= 0)
		return 0;

	const uint32_t Value = (Pixel & Mask) >> Shift;
	if(Bits >= 8)
		return (uint8_t)(Value >> (Bits - 8));

	const uint32_t MaxValue = (1u << Bits) - 1u;
	return MaxValue == 0 ? 0 : (uint8_t)((Value * 255u) / MaxValue);
}

bool ConvertImageInfoToRgba(const CImageInfo &Src, SClipboardImage &OutImage)
{
	if(Src.m_Width <= 0 || Src.m_Height <= 0 || Src.m_pData == nullptr)
		return false;

	const int Width = (int)Src.m_Width;
	const int Height = (int)Src.m_Height;
	OutImage.m_Width = Width;
	OutImage.m_Height = Height;
	OutImage.m_vRgba.resize((size_t)Width * Height * 4);

	const size_t PixelSize = CImageInfo::PixelSize(Src.m_Format);
	for(int y = 0; y < Height; ++y)
	{
		for(int x = 0; x < Width; ++x)
		{
			const size_t SrcIndex = ((size_t)y * Width + x) * PixelSize;
			uint8_t *pDst = OutImage.m_vRgba.data() + ((size_t)y * Width + x) * 4;
			switch(Src.m_Format)
			{
			case CImageInfo::FORMAT_RGBA:
				pDst[0] = Src.m_pData[SrcIndex + 0];
				pDst[1] = Src.m_pData[SrcIndex + 1];
				pDst[2] = Src.m_pData[SrcIndex + 2];
				pDst[3] = Src.m_pData[SrcIndex + 3];
				break;
			case CImageInfo::FORMAT_RGB:
				pDst[0] = Src.m_pData[SrcIndex + 0];
				pDst[1] = Src.m_pData[SrcIndex + 1];
				pDst[2] = Src.m_pData[SrcIndex + 2];
				pDst[3] = 255;
				break;
			default:
				return false;
			}
		}
	}

	return OutImage.IsValid();
}

bool DecodeClipboardDib(const void *pData, size_t DataSize, SClipboardImage &OutImage)
{
	OutImage = {};
	if(pData == nullptr || DataSize < sizeof(SBitmapInfoHeader))
		return false;

	const auto *pHeader = static_cast<const SBitmapInfoHeader *>(pData);
	if(pHeader->m_Size < sizeof(SBitmapInfoHeader) || DataSize < pHeader->m_Size || pHeader->m_Planes != 1)
		return false;
	if(pHeader->m_BitCount != 24 && pHeader->m_BitCount != 32)
		return false;
	if(pHeader->m_Width <= 0 || pHeader->m_Height == 0)
		return false;
	if(pHeader->m_Compression != DIB_COMPRESSION_RGB && pHeader->m_Compression != DIB_COMPRESSION_BITFIELDS)
		return false;
	if(pHeader->m_BitCount == 24 && pHeader->m_Compression != DIB_COMPRESSION_RGB)
		return false;
	if(pHeader->m_ClrUsed != 0 && pHeader->m_BitCount <= 8)
		return false;

	const int Width = pHeader->m_Width;
	const int Height = pHeader->m_Height < 0 ? -pHeader->m_Height : pHeader->m_Height;
	const bool TopDown = pHeader->m_Height < 0;
	const size_t HeaderSize = pHeader->m_Size;
	const size_t RowStride = ((size_t)Width * pHeader->m_BitCount + 31u) / 32u * 4u;

	uint32_t RedMask = 0x00ff0000u;
	uint32_t GreenMask = 0x0000ff00u;
	uint32_t BlueMask = 0x000000ffu;
	uint32_t AlphaMask = 0u;
	size_t PixelOffset = HeaderSize;

	if(pHeader->m_Compression == DIB_COMPRESSION_BITFIELDS)
	{
		if(HeaderSize >= sizeof(SBitmapV4Header))
		{
			const auto *pHeaderV4 = static_cast<const SBitmapV4Header *>(pData);
			RedMask = pHeaderV4->m_RedMask;
			GreenMask = pHeaderV4->m_GreenMask;
			BlueMask = pHeaderV4->m_BlueMask;
			AlphaMask = pHeaderV4->m_AlphaMask;
		}
		else
		{
			if(DataSize < HeaderSize + sizeof(uint32_t) * 3)
				return false;
			const auto *pMasks = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(pData) + HeaderSize);
			RedMask = pMasks[0];
			GreenMask = pMasks[1];
			BlueMask = pMasks[2];
			PixelOffset += sizeof(uint32_t) * 3;
		}
	}

	if(PixelOffset + RowStride * (size_t)Height > DataSize)
		return false;

	const uint8_t *pPixels = static_cast<const uint8_t *>(pData) + PixelOffset;
	OutImage.m_Width = Width;
	OutImage.m_Height = Height;
	OutImage.m_vRgba.resize((size_t)Width * Height * 4);

	for(int y = 0; y < Height; ++y)
	{
		const int SourceY = TopDown ? y : (Height - 1 - y);
		const uint8_t *pRow = pPixels + RowStride * (size_t)SourceY;
		uint8_t *pDst = OutImage.m_vRgba.data() + (size_t)y * Width * 4;
		for(int x = 0; x < Width; ++x)
		{
			if(pHeader->m_BitCount == 24)
			{
				const uint8_t *pSrc = pRow + x * 3;
				pDst[x * 4 + 0] = pSrc[2];
				pDst[x * 4 + 1] = pSrc[1];
				pDst[x * 4 + 2] = pSrc[0];
				pDst[x * 4 + 3] = 255;
			}
			else
			{
				const uint8_t *pSrc = pRow + x * 4;
				const uint32_t Pixel = (uint32_t)pSrc[0] | ((uint32_t)pSrc[1] << 8) | ((uint32_t)pSrc[2] << 16) | ((uint32_t)pSrc[3] << 24);
				if(pHeader->m_Compression == DIB_COMPRESSION_RGB)
				{
					pDst[x * 4 + 0] = pSrc[2];
					pDst[x * 4 + 1] = pSrc[1];
					pDst[x * 4 + 2] = pSrc[0];
					pDst[x * 4 + 3] = 255;
				}
				else
				{
					pDst[x * 4 + 0] = ExtractMaskedChannel(Pixel, RedMask);
					pDst[x * 4 + 1] = ExtractMaskedChannel(Pixel, GreenMask);
					pDst[x * 4 + 2] = ExtractMaskedChannel(Pixel, BlueMask);
					pDst[x * 4 + 3] = AlphaMask != 0 ? ExtractMaskedChannel(Pixel, AlphaMask) : 255;
				}
			}
		}
	}

	return OutImage.IsValid();
}

bool OpenClipboardForRead()
{
	for(int Attempt = 0; Attempt < 20; ++Attempt)
	{
		// Reading with a null owner is the most reliable approach on Windows.
		if(OpenClipboard(nullptr))
			return true;

		if(s_ClipboardOwnerWindow != nullptr && OpenClipboard(s_ClipboardOwnerWindow))
			return true;

		HWND hForeground = GetForegroundWindow();
		if(hForeground != nullptr && OpenClipboard(hForeground))
			return true;

		HWND hWnd = GetActiveWindow();
		if(hWnd != nullptr && hWnd != hForeground && OpenClipboard(hWnd))
			return true;

		Sleep(15);
	}

	return false;
}

bool KnownImageClipboardFormatAvailable()
{
	if(IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_HDROP))
		return true;

	static UINT s_HtmlFormat = 0;
	if(s_HtmlFormat == 0)
		s_HtmlFormat = RegisterClipboardFormatW(L"HTML Format");
	if(s_HtmlFormat != 0 && IsClipboardFormatAvailable(s_HtmlFormat))
		return true;

	static UINT s_aKnownFormats[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
		s_aKnownFormats[3] = RegisterClipboardFormatW(L"JFIF");
		s_aKnownFormats[4] = RegisterClipboardFormatW(L"image/jpeg");
		s_aKnownFormats[5] = RegisterClipboardFormatW(L"GIF");
		s_aKnownFormats[6] = RegisterClipboardFormatW(L"image/webp");
		s_aKnownFormats[7] = RegisterClipboardFormatW(L"WEBP");
	}

	for(int i = 0; i < 8; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat != 0 && IsClipboardFormatAvailable(ClipboardFormat))
			return true;
	}

	return false;
}

bool ReadClipboardBitmapOpen(SClipboardImage &OutImage)
{
	if(!IsClipboardFormatAvailable(CF_BITMAP))
		return false;

	HBITMAP hBitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
	if(hBitmap == nullptr)
		return false;

	BITMAP BitmapInfo;
	if(GetObject(hBitmap, sizeof(BitmapInfo), &BitmapInfo) != sizeof(BitmapInfo))
		return false;
	if(BitmapInfo.bmWidth <= 0 || BitmapInfo.bmHeight <= 0)
		return false;

	const int Width = BitmapInfo.bmWidth;
	const int Height = BitmapInfo.bmHeight;

	BITMAPINFO Info = {};
	Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	Info.bmiHeader.biWidth = Width;
	Info.bmiHeader.biHeight = -Height;
	Info.bmiHeader.biPlanes = 1;
	Info.bmiHeader.biBitCount = 32;
	Info.bmiHeader.biCompression = BI_RGB;

	std::vector<uint8_t> vPixels((size_t)Width * Height * 4);
	HDC hDc = GetDC(nullptr);
	const int LinesRead = GetDIBits(hDc, hBitmap, 0, Height, vPixels.data(), &Info, DIB_RGB_COLORS);
	ReleaseDC(nullptr, hDc);
	if(LinesRead != Height)
		return false;

	OutImage.m_Width = Width;
	OutImage.m_Height = Height;
	OutImage.m_vRgba.resize(vPixels.size());
	for(size_t i = 0; i < vPixels.size(); i += 4)
	{
		OutImage.m_vRgba[i + 0] = vPixels[i + 2];
		OutImage.m_vRgba[i + 1] = vPixels[i + 1];
		OutImage.m_vRgba[i + 2] = vPixels[i + 0];
		OutImage.m_vRgba[i + 3] = 255;
	}

	return OutImage.IsValid();
}

bool FindPngOffset(const uint8_t *pData, size_t Size, size_t &OutOffset)
{
	if(pData == nullptr || Size < 8)
		return false;

	for(size_t i = 0; i + 8 <= Size; ++i)
	{
		if(pData[i] == 0x89 && pData[i + 1] == 'P' && pData[i + 2] == 'N' && pData[i + 3] == 'G')
		{
			OutOffset = i;
			return true;
		}
	}

	return false;
}

bool DecodePngBytes(const uint8_t *pData, size_t Size, SClipboardImage &OutImage)
{
	CByteBufferReader Reader(pData, Size);
	CImageInfo LoadedImage;
	int PngliteIncompatible = 0;
	if(!CImageLoader::LoadPng(Reader, "clipboard-png", LoadedImage, PngliteIncompatible))
		return false;

	const bool Success = ConvertImageInfoToRgba(LoadedImage, OutImage);
	LoadedImage.Free();
	return Success;
}

bool ReadClipboardPngOpen(SClipboardImage &OutImage)
{
	static UINT s_aKnownFormats[3] = {0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
	}

	for(int i = 0; i < 3; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat == 0 || !IsClipboardFormatAvailable(ClipboardFormat))
			continue;

		HANDLE hClipboard = GetClipboardData(ClipboardFormat);
		if(hClipboard == nullptr)
			continue;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 8)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			continue;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t PngOffset = 0;
		const uint8_t *pPngData = pBytes;
		size_t PngSize = (size_t)ClipboardSize;
		if(!FindPngOffset(pBytes, (size_t)ClipboardSize, PngOffset))
		{
			GlobalUnlock(hClipboard);
			continue;
		}

		pPngData = pBytes + PngOffset;
		PngSize = (size_t)ClipboardSize - PngOffset;
		const bool Success = DecodePngBytes(pPngData, PngSize, OutImage);
		GlobalUnlock(hClipboard);
		if(Success)
			return true;
	}

	for(UINT ClipboardFormat = EnumClipboardFormats(0); ClipboardFormat != 0; ClipboardFormat = EnumClipboardFormats(ClipboardFormat))
	{
		HANDLE hClipboard = GetClipboardData(ClipboardFormat);
		if(hClipboard == nullptr)
			continue;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 8)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			continue;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t PngOffset = 0;
		if(!FindPngOffset(pBytes, (size_t)ClipboardSize, PngOffset))
		{
			GlobalUnlock(hClipboard);
			continue;
		}

		const bool Success = DecodePngBytes(pBytes + PngOffset, (size_t)ClipboardSize - PngOffset, OutImage);
		GlobalUnlock(hClipboard);
		if(Success)
			return true;
	}

	return false;
}

bool ReadClipboardDibOpen(SClipboardImage &OutImage)
{
	HANDLE hClipboard = GetClipboardData(CF_DIBV5);
	if(hClipboard == nullptr)
		hClipboard = GetClipboardData(CF_DIB);
	if(hClipboard == nullptr)
	{
		// Delay-rendered clipboard data can appear one message pump later.
		Sleep(0);
		hClipboard = GetClipboardData(CF_DIBV5);
		if(hClipboard == nullptr)
			hClipboard = GetClipboardData(CF_DIB);
	}
	if(hClipboard == nullptr)
		return false;

	void *pClipboardData = GlobalLock(hClipboard);
	const SIZE_T ClipboardSize = GlobalSize(hClipboard);
	const bool Success = pClipboardData != nullptr && ClipboardSize > 0 && DecodeClipboardDib(pClipboardData, (size_t)ClipboardSize, OutImage);
	if(pClipboardData != nullptr)
		GlobalUnlock(hClipboard);
	return Success;
}

bool ClipboardHasImageFormatsOpen()
{
	if(KnownImageClipboardFormatAvailable())
		return true;

	for(UINT ClipboardFormat = EnumClipboardFormats(0); ClipboardFormat != 0; ClipboardFormat = EnumClipboardFormats(ClipboardFormat))
	{
		HANDLE hClipboard = GetClipboardData(ClipboardFormat);
		if(hClipboard == nullptr)
			continue;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData != nullptr && ClipboardSize >= 8)
		{
			size_t PngOffset = 0;
			const bool HasPng = FindPngOffset(static_cast<const uint8_t *>(pClipboardData), (size_t)ClipboardSize, PngOffset);
			GlobalUnlock(hClipboard);
			if(HasPng)
				return true;
		}
		else if(pClipboardData != nullptr)
		{
			GlobalUnlock(hClipboard);
		}
	}

	return false;
}

bool IsGifSignature(const uint8_t *pData, size_t Size)
{
	return pData != nullptr && Size >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

bool IsValidGifFile(const uint8_t *pData, size_t Size)
{
	if(!IsGifSignature(pData, Size) || Size < 13)
		return false;

	const uint16_t Width = (uint16_t)(pData[6] | (pData[7] << 8));
	const uint16_t Height = (uint16_t)(pData[8] | (pData[9] << 8));
	return Width > 0 && Height > 0 && Width <= 8192 && Height <= 8192;
}

bool AssignValidatedGifBytes(const uint8_t *pData, size_t Size, std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	if(pData == nullptr || Size > MaxBytes || !IsValidGifFile(pData, Size))
		return false;
	OutBytes.assign(pData, pData + Size);
	return true;
}

bool IsJpegSignature(const uint8_t *pData, size_t Size)
{
	return pData != nullptr && Size >= 3 && pData[0] == 0xFF && pData[1] == 0xD8 && pData[2] == 0xFF;
}

bool IsWebpSignature(const uint8_t *pData, size_t Size)
{
	return pData != nullptr && Size >= 12 &&
		mem_comp(pData, "RIFF", 4) == 0 &&
		mem_comp(pData + 8, "WEBP", 4) == 0;
}

bool IsPngFileSignature(const uint8_t *pData, size_t Size)
{
	return pData != nullptr && Size >= 8 &&
		pData[0] == 0x89 && pData[1] == 'P' && pData[2] == 'N' && pData[3] == 'G';
}

EClipboardPastedKind DetectImageKindFromBytes(const uint8_t *pData, size_t Size)
{
	if(IsGifSignature(pData, Size))
		return EClipboardPastedKind::GIF;
	if(IsPngFileSignature(pData, Size))
		return EClipboardPastedKind::PNG;
	if(IsJpegSignature(pData, Size))
		return EClipboardPastedKind::JPEG;
	if(IsWebpSignature(pData, Size))
		return EClipboardPastedKind::WEBP;
	return EClipboardPastedKind::NONE;
}

bool ImagePathExtensionKind(const wchar_t *pPath, EClipboardPastedKind &OutKind)
{
	if(pPath == nullptr || pPath[0] == '\0')
		return false;

	const wchar_t *pExt = wcsrchr(pPath, L'.');
	if(pExt == nullptr)
		return false;

	wchar_t aExt[16] = {};
	wcsncpy_s(aExt, pExt, _TRUNCATE);
	for(wchar_t *pCh = aExt; *pCh; ++pCh)
		*pCh = (wchar_t)towlower(*pCh);

	if(!wcscmp(aExt, L".gif"))
		OutKind = EClipboardPastedKind::GIF;
	else if(!wcscmp(aExt, L".png"))
		OutKind = EClipboardPastedKind::PNG;
	else if(!wcscmp(aExt, L".jpg") || !wcscmp(aExt, L".jpeg"))
		OutKind = EClipboardPastedKind::JPEG;
	else if(!wcscmp(aExt, L".webp"))
		OutKind = EClipboardPastedKind::WEBP;
	else
		return false;

	return true;
}

bool ReadImageFileBytes(const wchar_t *pPath, EClipboardPastedKind ExpectedKind, std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	OutBytes.clear();
	if(pPath == nullptr || pPath[0] == '\0')
		return false;

	HANDLE hFile = CreateFileW(pPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(hFile == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER FileSize = {};
	if(!GetFileSizeEx(hFile, &FileSize) || FileSize.QuadPart <= 0 || (size_t)FileSize.QuadPart > MaxBytes)
	{
		CloseHandle(hFile);
		return false;
	}

	OutBytes.resize((size_t)FileSize.QuadPart);
	DWORD ReadBytes = 0;
	const BOOL Ok = ReadFile(hFile, OutBytes.data(), (DWORD)OutBytes.size(), &ReadBytes, nullptr);
	CloseHandle(hFile);
	if(!Ok || ReadBytes == 0)
	{
		OutBytes.clear();
		return false;
	}

	OutBytes.resize(ReadBytes);
	const EClipboardPastedKind DetectedKind = DetectImageKindFromBytes(OutBytes.data(), OutBytes.size());
	if(DetectedKind == EClipboardPastedKind::NONE)
		return false;
	if(ExpectedKind != EClipboardPastedKind::NONE && DetectedKind != ExpectedKind)
		return false;
	return true;
}

bool ReadFileBytes(const wchar_t *pPath, std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	return ReadImageFileBytes(pPath, EClipboardPastedKind::GIF, OutBytes, MaxBytes);
}

const char *FindSubstrInBuffer(const char *pHaystack, size_t HaystackLen, const char *pNeedle)
{
	if(pHaystack == nullptr || pNeedle == nullptr)
		return nullptr;

	const size_t NeedleLen = str_length(pNeedle);
	if(NeedleLen == 0 || HaystackLen < NeedleLen)
		return nullptr;

	for(size_t i = 0; i + NeedleLen <= HaystackLen; ++i)
	{
		if(mem_comp(pHaystack + i, pNeedle, NeedleLen) == 0)
			return pHaystack + i;
	}

	return nullptr;
}

bool ReadClipboardHtmlImageOpen(std::vector<uint8_t> &OutBytes, EClipboardPastedKind &OutKind, size_t MaxBytes)
{
	OutBytes.clear();
	OutKind = EClipboardPastedKind::NONE;

	static UINT s_HtmlFormat = 0;
	if(s_HtmlFormat == 0)
		s_HtmlFormat = RegisterClipboardFormatW(L"HTML Format");
	if(s_HtmlFormat == 0 || !IsClipboardFormatAvailable(s_HtmlFormat))
		return false;

	HANDLE hClipboard = GetClipboardData(s_HtmlFormat);
	if(hClipboard == nullptr)
		return false;

	const char *pData = static_cast<const char *>(GlobalLock(hClipboard));
	const SIZE_T DataSize = GlobalSize(hClipboard);
	if(pData == nullptr || DataSize < 16)
	{
		if(pData != nullptr)
			GlobalUnlock(hClipboard);
		return false;
	}

	bool Success = false;
	const char *pSearch = pData;
	const char *pEnd = pData + DataSize;
	while(pSearch < pEnd)
	{
		const char *pMarker = FindSubstrInBuffer(pSearch, (size_t)(pEnd - pSearch), "data:image/");
		if(pMarker == nullptr)
			break;

		const char *pBase64 = FindSubstrInBuffer(pMarker, (size_t)(pEnd - pMarker), "base64,");
		if(pBase64 == nullptr)
		{
			pSearch = pMarker + 11;
			continue;
		}
		pBase64 += 7;

		const char *pPayloadEnd = pBase64;
		while(pPayloadEnd < pEnd && *pPayloadEnd != '"' && *pPayloadEnd != '\'' && *pPayloadEnd != ' ' && *pPayloadEnd != '>' && *pPayloadEnd != '\r' && *pPayloadEnd != '\n')
			pPayloadEnd++;

		const size_t EncodedLen = (size_t)(pPayloadEnd - pBase64);
		if(EncodedLen == 0)
		{
			pSearch = pBase64;
			continue;
		}

		std::vector<char> aEncoded(EncodedLen + 1);
		mem_copy(aEncoded.data(), pBase64, EncodedLen);
		aEncoded[EncodedLen] = '\0';

		std::vector<uint8_t> Decoded(EncodedLen);
		const int DecodedSize = str_base64_decode(Decoded.data(), (int)Decoded.size(), aEncoded.data());
		if(DecodedSize <= 0)
		{
			pSearch = pBase64;
			continue;
		}

		Decoded.resize(DecodedSize);
		const EClipboardPastedKind Kind = DetectImageKindFromBytes(Decoded.data(), Decoded.size());
		if(Kind == EClipboardPastedKind::NONE || (size_t)DecodedSize > MaxBytes)
		{
			pSearch = pBase64;
			continue;
		}

		OutBytes = std::move(Decoded);
		OutKind = Kind;
		Success = true;
		break;
	}

	GlobalUnlock(hClipboard);
	return Success;
}

bool ReadClipboardUnicodePathOpen(std::vector<uint8_t> &OutBytes, EClipboardPastedKind &OutKind, size_t MaxBytes)
{
	OutBytes.clear();
	OutKind = EClipboardPastedKind::NONE;
	if(!IsClipboardFormatAvailable(CF_UNICODETEXT))
		return false;

	HANDLE hClipboard = GetClipboardData(CF_UNICODETEXT);
	if(hClipboard == nullptr)
		return false;

	const wchar_t *pText = static_cast<const wchar_t *>(GlobalLock(hClipboard));
	if(pText == nullptr)
		return false;

	wchar_t aPath[MAX_PATH * 4] = {};
	wcsncpy_s(aPath, pText, _TRUNCATE);
	GlobalUnlock(hClipboard);

	wchar_t *pPath = aPath;
	while(*pPath != L'\0' && (*pPath == L' ' || *pPath == L'\t' || *pPath == L'\r' || *pPath == L'\n'))
		pPath++;
	if(*pPath == L'"')
		pPath++;

	size_t PathLen = wcslen(pPath);
	while(PathLen > 0 && (pPath[PathLen - 1] == L' ' || pPath[PathLen - 1] == L'\t' || pPath[PathLen - 1] == L'\r' || pPath[PathLen - 1] == L'\n' || pPath[PathLen - 1] == L'"'))
		pPath[--PathLen] = L'\0';

	if(wcsncmp(pPath, L"file:///", 8) == 0)
		pPath += 8;
	else if(wcsncmp(pPath, L"file://", 7) == 0)
		pPath += 7;

	EClipboardPastedKind PathKind = EClipboardPastedKind::NONE;
	if(!ImagePathExtensionKind(pPath, PathKind))
		return false;
	if(!ReadImageFileBytes(pPath, PathKind, OutBytes, MaxBytes))
		return false;

	OutKind = DetectImageKindFromBytes(OutBytes.data(), OutBytes.size());
	if(OutKind == EClipboardPastedKind::NONE)
		OutKind = PathKind;
	return true;
}

bool ReadClipboardGifFromHDropOpen(std::vector<uint8_t> &OutBytes, size_t MaxBytes, char *pFileName = nullptr, size_t FileNameSize = 0)
{
	if(pFileName != nullptr && FileNameSize > 0)
		pFileName[0] = '\0';
	if(!IsClipboardFormatAvailable(CF_HDROP))
	{
		log_info("clipboard", "GIF file paste: CF_HDROP is not available");
		return false;
	}

	HANDLE hDrop = GetClipboardData(CF_HDROP);
	if(hDrop == nullptr)
	{
		log_info("clipboard", "GIF file paste: GetClipboardData(CF_HDROP) failed (%lu)", GetLastError());
		return false;
	}

	const HDROP hDropList = static_cast<HDROP>(hDrop);

	bool Success = false;
	const UINT FileCount = DragQueryFileW(hDropList, 0xFFFFFFFF, nullptr, 0);
	log_info("clipboard", "GIF file paste: CF_HDROP contains %u file(s)", FileCount);
	for(UINT i = 0; i < FileCount && !Success; ++i)
	{
		wchar_t aPath[MAX_PATH] = {};
		if(DragQueryFileW(hDropList, i, aPath, std::size(aPath)) == 0)
			continue;
		Success = ReadFileBytes(aPath, OutBytes, MaxBytes);
		if(Success && pFileName != nullptr && FileNameSize > 0)
		{
			const wchar_t *pBaseName = wcsrchr(aPath, L'\\');
			pBaseName = pBaseName != nullptr ? pBaseName + 1 : aPath;
			WideCharToMultiByte(CP_UTF8, 0, pBaseName, -1, pFileName, (int)FileNameSize, nullptr, nullptr);
			pFileName[FileNameSize - 1] = '\0';
		}
		log_info("clipboard", "GIF file paste: file %u read %s (%zu bytes)", i, Success ? "successfully" : "failed", OutBytes.size());
	}

	return Success;
}

bool ReadClipboardImageFromHDropOpen(std::vector<uint8_t> &OutBytes, EClipboardPastedKind &OutKind, size_t MaxBytes)
{
	OutBytes.clear();
	OutKind = EClipboardPastedKind::NONE;
	if(!IsClipboardFormatAvailable(CF_HDROP))
		return false;

	HANDLE hDrop = GetClipboardData(CF_HDROP);
	if(hDrop == nullptr)
		return false;

	const HDROP hDropList = static_cast<HDROP>(hDrop);

	bool Success = false;
	const UINT FileCount = DragQueryFileW(hDropList, 0xFFFFFFFF, nullptr, 0);
	for(UINT i = 0; i < FileCount && !Success; ++i)
	{
		wchar_t aPath[MAX_PATH] = {};
		if(DragQueryFileW(hDropList, i, aPath, std::size(aPath)) == 0)
			continue;

		EClipboardPastedKind PathKind = EClipboardPastedKind::NONE;
		if(!ImagePathExtensionKind(aPath, PathKind))
			continue;

		std::vector<uint8_t> FileBytes;
		if(!ReadImageFileBytes(aPath, PathKind, FileBytes, MaxBytes))
			continue;

		OutBytes = std::move(FileBytes);
		OutKind = DetectImageKindFromBytes(OutBytes.data(), OutBytes.size());
		if(OutKind == EClipboardPastedKind::NONE)
			OutKind = PathKind;
		Success = true;
	}

	return Success;
}

bool ReadClipboardGifOpen(std::vector<uint8_t> &OutBytes, size_t MaxBytes, char *pFileName = nullptr, size_t FileNameSize = 0)
{
	OutBytes.clear();
	if(pFileName != nullptr && FileNameSize > 0)
		pFileName[0] = '\0';

	static UINT s_GifFormat = 0;
	if(s_GifFormat == 0)
		s_GifFormat = RegisterClipboardFormatW(L"GIF");
	if(s_GifFormat != 0 && IsClipboardFormatAvailable(s_GifFormat))
	{
		HANDLE hClipboard = GetClipboardData(s_GifFormat);
		if(hClipboard != nullptr)
		{
			void *pClipboardData = GlobalLock(hClipboard);
			const SIZE_T ClipboardSize = GlobalSize(hClipboard);
			if(pClipboardData != nullptr && ClipboardSize >= 6 && ClipboardSize <= MaxBytes)
			{
				const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
				std::vector<uint8_t> Candidate;
				if(AssignValidatedGifBytes(pBytes, (size_t)ClipboardSize, Candidate, MaxBytes))
					OutBytes = std::move(Candidate);
			}
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			if(!OutBytes.empty())
				return true;
		}
	}

	// Existing files copied by Explorer are exposed as CF_HDROP. Do not access
	// CFSTR_FILECONTENTS through GetClipboardData/GlobalLock: Shell data objects
	// commonly expose it as an IStream, and requesting it this way can invalidate
	// delayed-rendered clipboard formats before CF_HDROP is read.
	if(ReadClipboardGifFromHDropOpen(OutBytes, MaxBytes, pFileName, FileNameSize))
		return true;

	return false;
}

bool ReadClipboardJpegBytesOpen(std::vector<uint8_t> &OutBytes)
{
	OutBytes.clear();

	static UINT s_aKnownFormats[3] = {0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"JFIF");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/jpeg");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"JPEG");
	}

	auto TryHandle = [&](HANDLE hClipboard) -> bool {
		if(hClipboard == nullptr)
			return false;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 3)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			return false;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t Offset = 0;
		if(!IsJpegSignature(pBytes, (size_t)ClipboardSize))
		{
			bool Found = false;
			for(size_t i = 0; i + 3 <= (size_t)ClipboardSize; ++i)
			{
				if(IsJpegSignature(pBytes + i, (size_t)ClipboardSize - i))
				{
					Offset = i;
					Found = true;
					break;
				}
			}
			if(!Found)
			{
				GlobalUnlock(hClipboard);
				return false;
			}
		}

		OutBytes.assign(pBytes + Offset, pBytes + ClipboardSize);
		GlobalUnlock(hClipboard);
		return true;
	};

	for(int i = 0; i < 3; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat == 0 || !IsClipboardFormatAvailable(ClipboardFormat))
			continue;
		if(TryHandle(GetClipboardData(ClipboardFormat)))
			return true;
	}

	return false;
}

bool ReadClipboardWebpBytesOpen(std::vector<uint8_t> &OutBytes)
{
	OutBytes.clear();

	static UINT s_aKnownFormats[2] = {0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"image/webp");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"WEBP");
	}

	auto TryHandle = [&](HANDLE hClipboard) -> bool {
		if(hClipboard == nullptr)
			return false;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 12)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			return false;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t Offset = 0;
		if(!IsWebpSignature(pBytes, (size_t)ClipboardSize))
		{
			bool Found = false;
			for(size_t i = 0; i + 12 <= (size_t)ClipboardSize; ++i)
			{
				if(IsWebpSignature(pBytes + i, (size_t)ClipboardSize - i))
				{
					Offset = i;
					Found = true;
					break;
				}
			}
			if(!Found)
			{
				GlobalUnlock(hClipboard);
				return false;
			}
		}

		OutBytes.assign(pBytes + Offset, pBytes + ClipboardSize);
		GlobalUnlock(hClipboard);
		return true;
	};

	for(int i = 0; i < 2; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat == 0 || !IsClipboardFormatAvailable(ClipboardFormat))
			continue;
		if(TryHandle(GetClipboardData(ClipboardFormat)))
			return true;
	}

	return false;
}

bool ReadClipboardPngBytesOpen(std::vector<uint8_t> &OutBytes)
{
	OutBytes.clear();

	static UINT s_aKnownFormats[3] = {0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
	}

	auto TryHandle = [&](HANDLE hClipboard) -> bool {
		if(hClipboard == nullptr)
			return false;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 8)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			return false;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t PngOffset = 0;
		if(!FindPngOffset(pBytes, (size_t)ClipboardSize, PngOffset))
		{
			GlobalUnlock(hClipboard);
			return false;
		}

		OutBytes.assign(pBytes + PngOffset, pBytes + ClipboardSize);
		GlobalUnlock(hClipboard);
		return true;
	};

	for(int i = 0; i < 3; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat == 0 || !IsClipboardFormatAvailable(ClipboardFormat))
			continue;
		if(TryHandle(GetClipboardData(ClipboardFormat)))
			return true;
	}

	return false;
}

void SetClipboardOwnerWindowImpl(void *pSdlWindow)
{
	s_ClipboardOwnerWindow = nullptr;
	if(pSdlWindow == nullptr)
		return;

	SDL_SysWMinfo Info;
	SDL_VERSION(&Info.version);
	if(SDL_GetWindowWMInfo(static_cast<SDL_Window *>(pSdlWindow), &Info))
		s_ClipboardOwnerWindow = Info.info.win.window;
}
}

bool ClipboardHasImageFormats()
{
	// IsClipboardFormatAvailable works without opening the clipboard on Windows.
	if(KnownImageClipboardFormatAvailable())
		return true;

	if(!OpenClipboardForRead())
		return false;

	const bool HasImage = ClipboardHasImageFormatsOpen();
	CloseClipboard();
	return HasImage;
}

bool ReadClipboardImage(SClipboardImage &OutImage)
{
	OutImage = {};

	if(!OpenClipboardForRead())
		return false;

	const bool Success = ReadClipboardPngOpen(OutImage) || ReadClipboardDibOpen(OutImage) || ReadClipboardBitmapOpen(OutImage);
	CloseClipboard();
	return Success;
}

#ifdef UC_CLIPBOARD_RESTORE_NOGDI
#define NOGDI
#endif

void SetClipboardOwnerWindow(void *pSdlWindow)
{
	SetClipboardOwnerWindowImpl(pSdlWindow);
}

bool ReadClipboardPngBytes(std::vector<uint8_t> &vOutPng)
{
	vOutPng.clear();
	if(!OpenClipboardForRead())
		return false;

	const bool Success = ReadClipboardPngBytesOpen(vOutPng);
	CloseClipboard();
	return Success;
}

bool ReadClipboardGifBytes(std::vector<uint8_t> &vOutGif, char *pFileName, size_t FileNameSize)
{
	vOutGif.clear();
	if(pFileName != nullptr && FileNameSize > 0)
		pFileName[0] = '\0';
	if(!OpenClipboardForRead())
		return false;

	const bool Success = ReadClipboardGifOpen(vOutGif, 64 * 1024 * 1024, pFileName, FileNameSize);
	CloseClipboard();
	return Success;
}

bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out)
{
	Out = {};
	if(!OpenClipboardForRead())
		return false;

	auto FillPreview = [&]() {
		if(Out.m_Preview.IsValid())
			return;
		ReadClipboardPngOpen(Out.m_Preview) || ReadClipboardDibOpen(Out.m_Preview) || ReadClipboardBitmapOpen(Out.m_Preview);
	};

	const size_t MaxBytes = 64 * 1024 * 1024;
	std::vector<uint8_t> PngBytes;
	if(ReadClipboardPngBytesOpen(PngBytes) && !PngBytes.empty())
	{
		SClipboardImage PngPreview;
		if(DecodePngBytes(PngBytes.data(), PngBytes.size(), PngPreview))
		{
			Out.m_Kind = EClipboardPastedKind::PNG;
			Out.m_vFileBytes = std::move(PngBytes);
			Out.m_Preview = std::move(PngPreview);
			CloseClipboard();
			return true;
		}
	}

	if(ReadClipboardPngOpen(Out.m_Preview) || ReadClipboardDibOpen(Out.m_Preview) || ReadClipboardBitmapOpen(Out.m_Preview))
	{
		Out.m_Kind = EClipboardPastedKind::BITMAP;
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> JpegBytes;
	if(ReadClipboardJpegBytesOpen(JpegBytes) && !JpegBytes.empty())
	{
		Out.m_Kind = EClipboardPastedKind::JPEG;
		Out.m_vFileBytes = std::move(JpegBytes);
		FillPreview();
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> WebpBytes;
	if(ReadClipboardWebpBytesOpen(WebpBytes) && !WebpBytes.empty())
	{
		Out.m_Kind = EClipboardPastedKind::WEBP;
		Out.m_vFileBytes = std::move(WebpBytes);
		FillPreview();
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> HdropBytes;
	EClipboardPastedKind HdropKind = EClipboardPastedKind::NONE;
	if(ReadClipboardImageFromHDropOpen(HdropBytes, HdropKind, MaxBytes) && !HdropBytes.empty())
	{
		Out.m_Kind = HdropKind;
		Out.m_vFileBytes = std::move(HdropBytes);
		if(Out.m_Kind == EClipboardPastedKind::PNG)
			DecodePngBytes(Out.m_vFileBytes.data(), Out.m_vFileBytes.size(), Out.m_Preview);
		FillPreview();
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> HtmlBytes;
	EClipboardPastedKind HtmlKind = EClipboardPastedKind::NONE;
	if(ReadClipboardHtmlImageOpen(HtmlBytes, HtmlKind, MaxBytes) && !HtmlBytes.empty())
	{
		Out.m_Kind = HtmlKind;
		Out.m_vFileBytes = std::move(HtmlBytes);
		if(Out.m_Kind == EClipboardPastedKind::PNG)
			DecodePngBytes(Out.m_vFileBytes.data(), Out.m_vFileBytes.size(), Out.m_Preview);
		FillPreview();
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> PathBytes;
	EClipboardPastedKind PathKind = EClipboardPastedKind::NONE;
	if(ReadClipboardUnicodePathOpen(PathBytes, PathKind, MaxBytes) && !PathBytes.empty())
	{
		Out.m_Kind = PathKind;
		Out.m_vFileBytes = std::move(PathBytes);
		if(Out.m_Kind == EClipboardPastedKind::PNG)
			DecodePngBytes(Out.m_vFileBytes.data(), Out.m_vFileBytes.size(), Out.m_Preview);
		FillPreview();
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> GifBytes;
	if(ReadClipboardGifOpen(GifBytes, MaxBytes) && !GifBytes.empty())
	{
		Out.m_Kind = EClipboardPastedKind::GIF;
		Out.m_vFileBytes = std::move(GifBytes);
		FillPreview();
		CloseClipboard();
		return true;
	}

	CloseClipboard();
	return false;
}

#else

void SetClipboardOwnerWindow(void *pSdlWindow)
{
	(void)pSdlWindow;
}


bool ClipboardHasImageFormats()
{
	return false;
}

bool ReadClipboardImage(SClipboardImage &OutImage)
{
	(void)OutImage;
	return false;
}

bool ReadClipboardPngBytes(std::vector<uint8_t> &vOutPng)
{
	(void)vOutPng;
	return false;
}

bool ReadClipboardGifBytes(std::vector<uint8_t> &vOutGif, char *pFileName, size_t FileNameSize)
{
	(void)vOutGif;
	(void)pFileName;
	(void)FileNameSize;
	return false;
}

bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out)
{
	Out = {};
	return false;
}

#endif
