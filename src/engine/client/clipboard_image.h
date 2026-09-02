#ifndef ENGINE_CLIENT_CLIPBOARD_IMAGE_H
#define ENGINE_CLIENT_CLIPBOARD_IMAGE_H

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
	BITMAP,
};

struct SClipboardPastedMedia
{
	EClipboardPastedKind m_Kind = EClipboardPastedKind::NONE;
	std::vector<uint8_t> m_vFileBytes;
	SClipboardImage m_Preview;

	bool IsValid() const
	{
		return m_Kind != EClipboardPastedKind::NONE;
	}
};

bool ReadClipboardImage(SClipboardImage &Image);
bool ReadClipboardPngBytes(std::vector<uint8_t> &vOutPng);
bool ReadClipboardPastedMedia(SClipboardPastedMedia &Out);
bool ClipboardHasImageFormats();
void SetClipboardOwnerWindow(void *pSdlWindow);

#endif
