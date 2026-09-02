#include "clipboard_image.h"

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
	for(int Attempt = 0; Attempt < 8; ++Attempt)
	{
		if(OpenClipboard(nullptr))
			return true;
		if(s_ClipboardOwnerWindow != nullptr && OpenClipboard(s_ClipboardOwnerWindow))
			return true;

		HWND hWnd = GetForegroundWindow();
		if(hWnd != nullptr && OpenClipboard(hWnd))
			return true;

		hWnd = GetActiveWindow();
		if(hWnd != nullptr && OpenClipboard(hWnd))
			return true;

		Sleep(5);
	}

	return false;
}

bool KnownImageClipboardFormatAvailable()
{
	if(IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_BITMAP))
		return true;

	static UINT s_aKnownFormats[4] = {0, 0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
		s_aKnownFormats[3] = RegisterClipboardFormatW(L"JFIF");
	}

	for(int i = 0; i < 4; ++i)
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
	static UINT s_aKnownFormats[4] = {0, 0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
		s_aKnownFormats[3] = RegisterClipboardFormatW(L"JFIF");
	}

	for(int i = 0; i < 4; ++i)
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

bool FindGifOffset(const uint8_t *pData, size_t Size, size_t &OutOffset)
{
	if(pData == nullptr || Size < 6)
		return false;

	for(size_t i = 0; i + 6 <= Size; ++i)
	{
		if(IsGifSignature(pData + i, Size - i))
		{
			OutOffset = i;
			return true;
		}
	}

	return false;
}

bool ReadFileBytes(const wchar_t *pPath, std::vector<uint8_t> &OutBytes, size_t MaxBytes)
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
	return IsGifSignature(OutBytes.data(), OutBytes.size());
}

bool ReadClipboardGifFromHDropOpen(std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	if(!IsClipboardFormatAvailable(CF_HDROP))
		return false;

	HANDLE hDrop = GetClipboardData(CF_HDROP);
	if(hDrop == nullptr)
		return false;

	const HDROP hDropList = static_cast<HDROP>(GlobalLock(hDrop));
	if(hDropList == nullptr)
		return false;

	bool Success = false;
	const UINT FileCount = DragQueryFileW(hDropList, 0xFFFFFFFF, nullptr, 0);
	for(UINT i = 0; i < FileCount && !Success; ++i)
	{
		wchar_t aPath[MAX_PATH] = {};
		if(DragQueryFileW(hDropList, i, aPath, std::size(aPath)) == 0)
			continue;
		Success = ReadFileBytes(aPath, OutBytes, MaxBytes);
	}

	GlobalUnlock(hDrop);
	return Success;
}

bool ReadClipboardGifFromFileContentsOpen(std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	static UINT s_FileContentsFormat = 0;
	if(s_FileContentsFormat == 0)
		s_FileContentsFormat = RegisterClipboardFormatW(L"FileContents");
	if(s_FileContentsFormat == 0 || !IsClipboardFormatAvailable(s_FileContentsFormat))
		return false;

	HANDLE hClipboard = GetClipboardData(s_FileContentsFormat);
	if(hClipboard == nullptr)
		return false;

	void *pClipboardData = GlobalLock(hClipboard);
	const SIZE_T ClipboardSize = GlobalSize(hClipboard);
	if(pClipboardData == nullptr || ClipboardSize < 6 || ClipboardSize > MaxBytes)
	{
		if(pClipboardData != nullptr)
			GlobalUnlock(hClipboard);
		return false;
	}

	const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
	size_t GifOffset = 0;
	const uint8_t *pGifData = pBytes;
	size_t GifSize = (size_t)ClipboardSize;
	if(!IsGifSignature(pBytes, (size_t)ClipboardSize))
	{
		if(!FindGifOffset(pBytes, (size_t)ClipboardSize, GifOffset))
		{
			GlobalUnlock(hClipboard);
			return false;
		}
		pGifData = pBytes + GifOffset;
		GifSize = (size_t)ClipboardSize - GifOffset;
	}

	OutBytes.assign(pGifData, pGifData + GifSize);
	GlobalUnlock(hClipboard);
	return IsGifSignature(OutBytes.data(), OutBytes.size());
}

bool ReadClipboardGifOpen(std::vector<uint8_t> &OutBytes, size_t MaxBytes)
{
	OutBytes.clear();
	if(ReadClipboardGifFromFileContentsOpen(OutBytes, MaxBytes) || ReadClipboardGifFromHDropOpen(OutBytes, MaxBytes))
		return true;

	for(UINT ClipboardFormat = EnumClipboardFormats(0); ClipboardFormat != 0; ClipboardFormat = EnumClipboardFormats(ClipboardFormat))
	{
		HANDLE hClipboard = GetClipboardData(ClipboardFormat);
		if(hClipboard == nullptr)
			continue;

		void *pClipboardData = GlobalLock(hClipboard);
		const SIZE_T ClipboardSize = GlobalSize(hClipboard);
		if(pClipboardData == nullptr || ClipboardSize < 6 || ClipboardSize > MaxBytes)
		{
			if(pClipboardData != nullptr)
				GlobalUnlock(hClipboard);
			continue;
		}

		const auto *pBytes = static_cast<const uint8_t *>(pClipboardData);
		size_t GifOffset = 0;
		if(IsGifSignature(pBytes, (size_t)ClipboardSize))
		{
			OutBytes.assign(pBytes, pBytes + ClipboardSize);
			GlobalUnlock(hClipboard);
			return true;
		}

		if(FindGifOffset(pBytes, (size_t)ClipboardSize, GifOffset))
		{
			OutBytes.assign(pBytes + GifOffset, pBytes + ClipboardSize);
			GlobalUnlock(hClipboard);
			return true;
		}

		GlobalUnlock(hClipboard);
	}

	return false;
}

bool ReadClipboardPngBytesOpen(std::vector<uint8_t> &OutBytes)
{
	OutBytes.clear();

	static UINT s_aKnownFormats[4] = {0, 0, 0, 0};
	if(s_aKnownFormats[0] == 0)
	{
		s_aKnownFormats[0] = RegisterClipboardFormatW(L"PNG");
		s_aKnownFormats[1] = RegisterClipboardFormatW(L"image/png");
		s_aKnownFormats[2] = RegisterClipboardFormatW(L"PNG\r\n");
		s_aKnownFormats[3] = RegisterClipboardFormatW(L"JFIF");
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

	for(int i = 0; i < 4; ++i)
	{
		const UINT ClipboardFormat = s_aKnownFormats[i];
		if(ClipboardFormat == 0 || !IsClipboardFormatAvailable(ClipboardFormat))
			continue;
		if(TryHandle(GetClipboardData(ClipboardFormat)))
			return true;
	}

	for(UINT ClipboardFormat = EnumClipboardFormats(0); ClipboardFormat != 0; ClipboardFormat = EnumClipboardFormats(ClipboardFormat))
	{
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

bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out)
{
	Out = {};
	if(!OpenClipboardForRead())
		return false;

	const size_t MaxBytes = 64 * 1024 * 1024;
	std::vector<uint8_t> GifBytes;
	if(ReadClipboardGifOpen(GifBytes, MaxBytes) && !GifBytes.empty())
	{
		Out.m_Kind = EClipboardPastedKind::GIF;
		Out.m_vFileBytes = std::move(GifBytes);
		ReadClipboardPngOpen(Out.m_Preview) || ReadClipboardDibOpen(Out.m_Preview) || ReadClipboardBitmapOpen(Out.m_Preview);
		CloseClipboard();
		return true;
	}

	std::vector<uint8_t> PngBytes;
	if(ReadClipboardPngBytesOpen(PngBytes) && !PngBytes.empty())
	{
		Out.m_Kind = EClipboardPastedKind::PNG;
		Out.m_vFileBytes = std::move(PngBytes);
		DecodePngBytes(Out.m_vFileBytes.data(), Out.m_vFileBytes.size(), Out.m_Preview);
		CloseClipboard();
		return true;
	}

	if(ReadClipboardPngOpen(Out.m_Preview) || ReadClipboardDibOpen(Out.m_Preview) || ReadClipboardBitmapOpen(Out.m_Preview))
	{
		Out.m_Kind = EClipboardPastedKind::BITMAP;
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

bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out)
{
	Out = {};
	return false;
}

#endif
