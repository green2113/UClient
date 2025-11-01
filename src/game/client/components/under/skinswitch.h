#ifndef GAME_CLIENT_COMPONENTS_UNDER_SKINSWITCH_H
#define GAME_CLIENT_COMPONENTS_UNDER_SKINSWITCH_H

#include <game/client/component.h>
#include <engine/console.h>

class CSkinswitch : public CComponent
{
public:
    CSkinswitch() {}

    virtual void OnConsoleInit() override;

    virtual int Sizeof() const override { return sizeof(*this); }

    static void ConSkinChange(IConsole::IResult *pResult, void *pUserData);

    void ChangeSkinByName(const char *pTargetName);
};

#endif
