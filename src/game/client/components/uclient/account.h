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
	// True when this install has no account yet, so the menu can register or sign in.
	bool NeedsAccountSetup() const { return m_State == EState::BLOCKED_NOT_REGISTERED && !m_Registered; }
	const char *InstallId() const { return m_aInstallId; }
	const char *Secret() const { return m_aSecret; }
	const char *BanReason() const { return m_aBanReason; }
	int64_t BanExpiresAt() const { return m_BanExpiresAt; }
	const char *ErrorMessage() const { return m_aError; }
	const char *ErrorCode() const { return m_aErrorCode; }

	// Starts verification after assets are loaded so the HTTP timeout is not
	// eaten by the long component/image init phase.
	void StartAuth();

	// Platforms without the launcher create or sign in to an account here.
	// A successful response is stored as uclient_account.json.
	void RegisterAnonymous();
	void LoginEmail(const char *pEmail, const char *pPassword);

private:
	void LoadAccount();
	void SaveAccount() const;
	enum class ERequestKind
	{
		VERIFY,
		REGISTER,
		LOGIN,
	};

	void GenerateCredentials(bool NewInstallId);
	void BeginAccountRequest(const char *pPath, const char *pJson, ERequestKind Kind);
	void BeginVerify();
	void FinishRequest();
	void FinishSetupRequest(EHttpState HttpState, int Status, json_value *pRoot);
	bool VerifyGraceToken() const;
	void SetBlocked(EState State, const char *pMessage, const char *pErrorCode);
	void ApplySuccess(const json_value *pRoot);

	EState m_State = EState::PENDING;
	// Preserve the component layout after removing the former request enum.
	int m_AccountAbiPadding = 0;
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
	// Preserve the component layout after removing the former retry flag.
	bool m_AccountAbiFlagPadding = false;
	bool m_AuthStarted = false;
	ERequestKind m_RequestKind = ERequestKind::VERIFY;
};

#endif
