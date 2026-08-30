#include "account.h"

#include <base/secure.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/uuid_manager.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_OPENSSL)
#include <openssl/evp.h>
#endif

#include <string>

namespace
{
constexpr const char *ACCOUNT_FILE = "uclient_account.json";
constexpr unsigned char GRACE_PUBLIC_KEY[32] = {
	0x7f, 0xff, 0x4e, 0x8e, 0x74, 0xee, 0xc7, 0x1e,
	0xa9, 0x59, 0x85, 0x7b, 0x49, 0x34, 0x20, 0x2e,
	0x1f, 0x34, 0xaa, 0x6b, 0x48, 0xf3, 0x94, 0x72,
	0x8b, 0x75, 0x11, 0xac, 0xaf, 0x33, 0xfe, 0x59};

// Stable support codes shown on the account gate.
constexpr int ACCOUNT_ERROR_NETWORK = 101;
constexpr int ACCOUNT_ERROR_ABORTED = 102;
constexpr int ACCOUNT_ERROR_INVALID_RESPONSE = 103;
constexpr int ACCOUNT_ERROR_INVALID_REQUEST = 201;
constexpr int ACCOUNT_ERROR_NOT_FOUND = 202;
constexpr int ACCOUNT_ERROR_BAD_SECRET = 203;
constexpr int ACCOUNT_ERROR_RATE_LIMITED = 205;
constexpr int ACCOUNT_ERROR_ALREADY_EXISTS = 206;
constexpr int ACCOUNT_ERROR_SERVER = 299;

const char *JsonString(const json_value *pObject, const char *pKey)
{
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue && pValue->type == json_string ? pValue->u.string.ptr : "";
}

int64_t JsonInteger(const json_value *pObject, const char *pKey)
{
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue && pValue->type == json_integer ? pValue->u.integer : 0;
}

bool JsonBool(const json_value *pObject, const char *pKey)
{
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue && pValue->type == json_boolean && pValue->u.boolean;
}

std::string JsonEscape(const char *pText)
{
	std::string Result;
	for(const unsigned char *pChar = (const unsigned char *)(pText ? pText : ""); *pChar; ++pChar)
	{
		switch(*pChar)
		{
		case '"': Result += "\\\""; break;
		case '\\': Result += "\\\\"; break;
		case '\n': Result += "\\n"; break;
		case '\r': Result += "\\r"; break;
		case '\t': Result += "\\t"; break;
		default:
			if(*pChar >= 0x20)
				Result += (char)*pChar;
		}
	}
	return Result;
}

int DecodeBase64Url(const std::string &Encoded, unsigned char *pOutput, int OutputSize)
{
	std::string Standard = Encoded;
	for(char &Character : Standard)
	{
		if(Character == '-')
			Character = '+';
		else if(Character == '_')
			Character = '/';
	}
	while(Standard.size() % 4 != 0)
		Standard.push_back('=');
	return str_base64_decode(pOutput, OutputSize, Standard.c_str());
}

bool VerifyEd25519(const unsigned char *pMessage, size_t MessageSize, const unsigned char *pSignature, size_t SignatureSize)
{
#if defined(CONF_OPENSSL)
	EVP_PKEY *pKey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, GRACE_PUBLIC_KEY, sizeof(GRACE_PUBLIC_KEY));
	if(!pKey)
		return false;
	EVP_MD_CTX *pContext = EVP_MD_CTX_new();
	const bool Valid = pContext &&
		EVP_DigestVerifyInit(pContext, nullptr, nullptr, nullptr, pKey) == 1 &&
		EVP_DigestVerify(pContext, pSignature, SignatureSize, pMessage, MessageSize) == 1;
	EVP_MD_CTX_free(pContext);
	EVP_PKEY_free(pKey);
	return Valid;
#else
	(void)pMessage;
	(void)MessageSize;
	(void)pSignature;
	(void)SignatureSize;
	return false;
#endif
}
}

void CUClientAccount::OnInit()
{
	LoadAccount();
	if(!m_HadAccountFile)
		GenerateIdentity(g_Config.m_UcInstallUuid[0] != '\0');
	// HTTP register/verify is deferred to StartAuth() after asset loading.
}

void CUClientAccount::StartAuth()
{
	if(m_AuthStarted || m_State != EState::PENDING || m_pRequest)
		return;
	m_AuthStarted = true;
	if(m_HadAccountFile && m_Registered)
		BeginVerify();
	else
		BeginRegister();
}

void CUClientAccount::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
	SaveAccount();
}

void CUClientAccount::OnUpdate()
{
	if(!m_AuthStarted && m_State == EState::PENDING)
		StartAuth();
	if(m_pRequest && m_pRequest->Done())
		FinishRequest();
	if(m_State != EState::OK &&
		(Client()->State() == IClient::STATE_CONNECTING ||
			Client()->State() == IClient::STATE_LOADING ||
			Client()->State() == IClient::STATE_ONLINE))
	{
		Client()->Disconnect();
	}
}

void CUClientAccount::LoadAccount()
{
	IOHANDLE File = Storage()->OpenFile(ACCOUNT_FILE, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aBuffer[2048];
	const unsigned Length = io_read(File, aBuffer, sizeof(aBuffer) - 1);
	io_close(File);
	aBuffer[Length] = '\0';
	json_value *pRoot = json_parse(aBuffer, Length);
	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		return;
	}
	str_copy(m_aInstallId, JsonString(pRoot, "install_uuid"), sizeof(m_aInstallId));
	str_copy(m_aSecret, JsonString(pRoot, "secret"), sizeof(m_aSecret));
	str_copy(m_aGraceToken, JsonString(pRoot, "grace_token"), sizeof(m_aGraceToken));
	m_GraceExpiresAt = JsonInteger(pRoot, "grace_expires_at");
	m_Registered = JsonBool(pRoot, "registered") || m_aGraceToken[0] != '\0';
	json_value_free(pRoot);
	m_HadAccountFile = m_aInstallId[0] != '\0' && m_aSecret[0] != '\0';
	if(m_HadAccountFile)
		str_copy(g_Config.m_UcInstallUuid, m_aInstallId, sizeof(g_Config.m_UcInstallUuid));
}

void CUClientAccount::SaveAccount() const
{
	if(!m_aInstallId[0] || !m_aSecret[0])
		return;
	IOHANDLE File = Storage()->OpenFile(ACCOUNT_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aBuffer[1536];
	str_format(aBuffer, sizeof(aBuffer),
		"{\"install_uuid\":\"%s\",\"secret\":\"%s\",\"grace_token\":\"%s\",\"grace_expires_at\":%lld,\"registered\":%s}",
		m_aInstallId, m_aSecret, m_aGraceToken, (long long)m_GraceExpiresAt, m_Registered ? "true" : "false");
	io_write(File, aBuffer, str_length(aBuffer));
	io_close(File);
}

void CUClientAccount::GenerateIdentity(bool KeepInstallId)
{
	if(!KeepInstallId || g_Config.m_UcInstallUuid[0] == '\0')
		FormatUuid(RandomUuid(), g_Config.m_UcInstallUuid, sizeof(g_Config.m_UcInstallUuid));
	str_copy(m_aInstallId, g_Config.m_UcInstallUuid, sizeof(m_aInstallId));
	secure_random_password(m_aSecret, sizeof(m_aSecret), 64);
	m_aGraceToken[0] = '\0';
	m_GraceExpiresAt = 0;
	m_Registered = false;
	SaveAccount();
}

void CUClientAccount::BeginRegister()
{
	char aUrl[384];
	str_format(aUrl, sizeof(aUrl), "%s/account/register", g_Config.m_UcApiBaseUrl);
	const std::string PlayerName = JsonEscape(Client()->PlayerName());
	char aJson[768];
	str_format(aJson, sizeof(aJson),
		"{\"install_id\":\"%s\",\"secret\":\"%s\",\"player_name\":\"%s\",\"version\":\"%s\"}",
		m_aInstallId, m_aSecret, PlayerName.c_str(), UCLIENT_VERSION);
	m_pRequest = HttpPostJson(aUrl, aJson);
	m_pRequest->FailOnErrorStatus(false);
	m_pRequest->Timeout(CTimeout{10000, 30000, 500, 5});
	m_Request = ERequest::REGISTER;
	m_State = EState::PENDING;
	m_aError[0] = '\0';
	m_aErrorCode[0] = '\0';
	Http()->Run(m_pRequest);
}

void CUClientAccount::BeginVerify()
{
	char aUrl[384];
	str_format(aUrl, sizeof(aUrl), "%s/account/verify", g_Config.m_UcApiBaseUrl);
	const std::string PlayerName = JsonEscape(Client()->PlayerName());
	char aJson[768];
	str_format(aJson, sizeof(aJson),
		"{\"install_id\":\"%s\",\"secret\":\"%s\",\"player_name\":\"%s\",\"version\":\"%s\"}",
		m_aInstallId, m_aSecret, PlayerName.c_str(), UCLIENT_VERSION);
	m_pRequest = HttpPostJson(aUrl, aJson);
	m_pRequest->FailOnErrorStatus(false);
	m_pRequest->Timeout(CTimeout{10000, 30000, 500, 5});
	m_Request = ERequest::VERIFY;
	m_State = EState::PENDING;
	m_aError[0] = '\0';
	m_aErrorCode[0] = '\0';
	Http()->Run(m_pRequest);
}

void CUClientAccount::ApplySuccess(const json_value *pRoot)
{
	str_copy(m_aGraceToken, JsonString(pRoot, "grace_token"), sizeof(m_aGraceToken));
	m_GraceExpiresAt = JsonInteger(pRoot, "grace_expires_at");
	m_Registered = true;
	m_State = EState::OK;
	m_aError[0] = '\0';
	m_aErrorCode[0] = '\0';
	str_copy(g_Config.m_UcInstallUuid, m_aInstallId, sizeof(g_Config.m_UcInstallUuid));
	SaveAccount();
	ConfigManager()->Save();
}

void CUClientAccount::SetBlocked(EState State, const char *pMessage, const char *pErrorCode)
{
	m_State = State;
	str_copy(m_aError, pMessage ? pMessage : "", sizeof(m_aError));
	str_copy(m_aErrorCode, pErrorCode ? pErrorCode : "", sizeof(m_aErrorCode));
	if(m_aErrorCode[0])
		dbg_msg("uclient_account", "account gate blocked error_code='%s'", m_aErrorCode);
}

void CUClientAccount::FinishRequest()
{
	const ERequest Request = m_Request;
	const EHttpState HttpState = m_pRequest->State();
	const int Status = HttpState == EHttpState::DONE ? m_pRequest->StatusCode() : 0;
	json_value *pRoot = HttpState == EHttpState::DONE ? m_pRequest->ResultJson() : nullptr;
	m_pRequest = nullptr;
	m_Request = ERequest::NONE;
	char aErrorCode[128];
	const auto FormatErrorCode = [&](int Code) {
		str_format(aErrorCode, sizeof(aErrorCode), "%d", Code);
	};

	if(HttpState != EHttpState::DONE)
	{
		if(VerifyGraceToken())
		{
			m_State = EState::OK;
			m_aError[0] = '\0';
			m_aErrorCode[0] = '\0';
		}
		else
		{
			FormatErrorCode(HttpState == EHttpState::ABORTED ? ACCOUNT_ERROR_ABORTED : ACCOUNT_ERROR_NETWORK);
			if(Request == ERequest::REGISTER)
			{
				SetBlocked(EState::BLOCKED_NOT_REGISTERED,
					Localize("Could not create a UClient account. Check your internet connection and try again."),
					aErrorCode);
			}
			else
			{
				SetBlocked(EState::BLOCKED_NOT_REGISTERED,
					Localize("UClient account verification is unavailable and the offline pass has expired."),
					aErrorCode);
			}
		}
		return;
	}
	if((Status == 200 || Status == 201) && pRoot && pRoot->type == json_object)
	{
		ApplySuccess(pRoot);
		json_value_free(pRoot);
		return;
	}
	if(Status == 423 && pRoot && pRoot->type == json_object)
	{
		str_copy(m_aBanReason, JsonString(pRoot, "reason"), sizeof(m_aBanReason));
		m_BanExpiresAt = JsonInteger(pRoot, "expires_at");
		m_aGraceToken[0] = '\0';
		m_GraceExpiresAt = 0;
		SaveAccount();
		SetBlocked(EState::BLOCKED_BANNED, Localize("Your UClient has been blocked by an administrator."), "");
		json_value_free(pRoot);
		return;
	}
	if(Request == ERequest::REGISTER && Status == 409 && !m_RetriedWithNewIdentity)
	{
		if(pRoot)
			json_value_free(pRoot);
		m_RetriedWithNewIdentity = true;
		GenerateIdentity(false);
		BeginRegister();
		return;
	}
	const char *pApiError = pRoot && pRoot->type == json_object ? JsonString(pRoot, "error") : "";
	int ErrorCode = ACCOUNT_ERROR_SERVER;
	if(Status >= 200 && Status < 300)
		ErrorCode = ACCOUNT_ERROR_INVALID_RESPONSE;
	else if(!str_comp(pApiError, "invalid_request"))
		ErrorCode = ACCOUNT_ERROR_INVALID_REQUEST;
	else if(!str_comp(pApiError, "account_not_found"))
		ErrorCode = ACCOUNT_ERROR_NOT_FOUND;
	else if(!str_comp(pApiError, "invalid_credentials"))
		ErrorCode = ACCOUNT_ERROR_BAD_SECRET;
	else if(!str_comp(pApiError, "registration_rate_limited"))
		ErrorCode = ACCOUNT_ERROR_RATE_LIMITED;
	else if(!str_comp(pApiError, "account_exists"))
		ErrorCode = ACCOUNT_ERROR_ALREADY_EXISTS;
	else if(Status == 400)
		ErrorCode = ACCOUNT_ERROR_INVALID_REQUEST;
	else if(Status == 404)
		ErrorCode = ACCOUNT_ERROR_NOT_FOUND;
	else if(Status == 403)
		ErrorCode = ACCOUNT_ERROR_BAD_SECRET;
	else if(Status == 429)
		ErrorCode = ACCOUNT_ERROR_RATE_LIMITED;
	else if(Status == 409)
		ErrorCode = ACCOUNT_ERROR_ALREADY_EXISTS;
	FormatErrorCode(ErrorCode);
	if(pRoot)
		json_value_free(pRoot);
	if(Status == 403)
		SetBlocked(EState::BLOCKED_BAD_SECRET, Localize("The UClient account secret is invalid."), aErrorCode);
	else
		SetBlocked(EState::BLOCKED_NOT_REGISTERED, Localize("This install UUID is not valid. If you changed it manually, restore the UUID that was originally assigned."), aErrorCode);
}

bool CUClientAccount::VerifyGraceToken() const
{
	if(!m_aGraceToken[0] || m_GraceExpiresAt <= time_timestamp())
		return false;
	const std::string Token = m_aGraceToken;
	const size_t Separator = Token.find('.');
	if(Separator == std::string::npos || Token.find('.', Separator + 1) != std::string::npos)
		return false;
	unsigned char aPayload[768];
	unsigned char aSignature[128];
	const int PayloadSize = DecodeBase64Url(Token.substr(0, Separator), aPayload, sizeof(aPayload) - 1);
	const int SignatureSize = DecodeBase64Url(Token.substr(Separator + 1), aSignature, sizeof(aSignature));
	if(PayloadSize <= 0 || SignatureSize != 64 ||
		!VerifyEd25519(aPayload, PayloadSize, aSignature, SignatureSize))
		return false;
	aPayload[PayloadSize] = '\0';
	json_value *pRoot = json_parse((const char *)aPayload, PayloadSize);
	if(!pRoot || pRoot->type != json_object)
	{
		if(pRoot)
			json_value_free(pRoot);
		return false;
	}
	const bool Valid = !str_comp(JsonString(pRoot, "install_id"), m_aInstallId) &&
		JsonInteger(pRoot, "exp") == m_GraceExpiresAt &&
		m_GraceExpiresAt > time_timestamp();
	json_value_free(pRoot);
	return Valid;
}
