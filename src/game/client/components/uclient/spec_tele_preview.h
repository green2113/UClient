#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_SPEC_TELE_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_SPEC_TELE_PREVIEW_H

#include <game/client/component.h>

class CUClientSpecTelePreview : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	bool OnInput(const IInput::CEvent &Event) override;
	void OnRender() override;

private:
	void UpdateHover();
	bool IsFeatureActive() const;

	bool m_HoverActive = false;
	int m_TeleNumber = 0;
	int m_OutIndex = 0;
	int m_OutCount = 0;
};

#endif
