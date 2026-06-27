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

bool ReadClipboardImage(SClipboardImage &Image);
bool ReadClipboardPngBytes(std::vector<uint8_t> &vOutPng);
bool ClipboardHasImageFormats();
void SetClipboardOwnerWindow(void *pSdlWindow);

#endif
