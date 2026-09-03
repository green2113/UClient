#ifndef ENGINE_CLIENT_CLIPBOARD_IMAGE_H
#define ENGINE_CLIENT_CLIPBOARD_IMAGE_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct SClipboardImage
{
	int m_Width = 0;
	int m_Height = 0;
	std::vector<uint8_t> m_vRgba;

	bool IsValid() const
	{
		return m_Width > 0 && m_Height > 0 && !m_vRgba.empty();
	}
};

enum class EClipboardPastedKind
{
	NONE = 0,
	PNG,
	GIF,
	JPEG,
	WEBP,
	BITMAP,
};

struct SClipboardPastedMedia
{
	EClipboardPastedKind m_Kind = EClipboardPastedKind::NONE;
	std::vector<uint8_t> m_vFileBytes;
	SClipboardImage m_Preview;
	char m_aFileName[256] = "";

	bool IsValid() const
	{
		return m_Kind != EClipboardPastedKind::NONE;
	}

	const char *UploadContentType() const
	{
		switch(m_Kind)
		{
		case EClipboardPastedKind::GIF: return "image/gif";
		case EClipboardPastedKind::JPEG: return "image/jpeg";
		case EClipboardPastedKind::WEBP: return "image/webp";
		case EClipboardPastedKind::PNG:
		case EClipboardPastedKind::BITMAP:
		default: return "image/png";
		}
	}
};

bool ReadClipboardImage(SClipboardImage &Image);
bool ReadClipboardPngBytes(std::vector<uint8_t> &vOutPng);
bool ReadClipboardGifBytes(std::vector<uint8_t> &vOutGif, char *pFileName = nullptr, size_t FileNameSize = 0);
bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out);
bool ClipboardHasImageFormats();
void SetClipboardOwnerWindow(void *pSdlWindow);

#endif
