#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_ACCOUNT_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_ACCOUNT_H

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <cstdint>
#include <memory>

class CUClientAccount : public CComponent
{
public:
	enum class EState
	{
		PENDING,
		OK,
		BLOCKED_NOT_REGISTERED,
		BLOCKED_BAD_SECRET,
		BLOCKED_BANNED,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;
	void OnShutdown() override;

	EState State() const { return m_State; }
	bool IsReady() const { return m_State == EState::OK; }
	bool IsPending() const { return m_State == EState::PENDING; }
	bool IsCreating() const { return m_Request == ERequest::REGISTER; }
	const char *InstallId() const { return m_aInstallId; }
	const char *Secret() const { return m_aSecret; }
	const char *BanReason() const { return m_aBanReason; }
	int64_t BanExpiresAt() const { return m_BanExpiresAt; }
	const char *ErrorMessage() const { return m_aError; }
	const char *ErrorCode() const { return m_aErrorCode; }

private:
	enum class ERequest
	{
		NONE,
		REGISTER,
		VERIFY,
	};

	void LoadAccount();
	void SaveAccount() const;
	void GenerateIdentity(bool KeepInstallId);
	void BeginRegister();
	void BeginVerify();
	void FinishRequest();
	bool VerifyGraceToken() const;
	void SetBlocked(EState State, const char *pMessage, const char *pErrorCode);
	void ApplySuccess(const json_value *pRoot);

	EState m_State = EState::PENDING;
	ERequest m_Request = ERequest::NONE;
	std::shared_ptr<CHttpRequest> m_pRequest;
	char m_aInstallId[64] = "";
	char m_aSecret[129] = "";
	char m_aGraceToken[1024] = "";
	int64_t m_GraceExpiresAt = 0;
	char m_aBanReason[256] = "";
	int64_t m_BanExpiresAt = 0;
	char m_aError[256] = "";
	char m_aErrorCode[128] = "";
	bool m_HadAccountFile = false;
	bool m_Registered = false;
	bool m_RetriedWithNewIdentity = false;
};

#endif
