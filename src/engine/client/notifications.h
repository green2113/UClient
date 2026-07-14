#ifndef ENGINE_CLIENT_NOTIFICATIONS_H
#define ENGINE_CLIENT_NOTIFICATIONS_H

#include <engine/notifications.h>

class CNotifications : public INotifications
{
	bool m_Initialized = false;
	char m_aAppName[64] = "";

public:
	void Init(const char *pAppname) override;
	void Shutdown() override;
	void Notify(const char *pTitle, const char *pMessage) override;
};

#endif // ENGINE_CLIENT_NOTIFICATIONS_H
