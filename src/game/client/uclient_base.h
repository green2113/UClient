#ifndef GAME_CLIENT_UCLIENT_BASE_H
#define GAME_CLIENT_UCLIENT_BASE_H

// Which fork the UClient features sit on.
// 0 = DDNet + UClient, 1 = TClient + UClient, 2 = BestClient + UClient.
#ifndef CONF_UCLIENT_BASE
#define CONF_UCLIENT_BASE 2
#endif

#define UCLIENT_BASE_DDNET 0
#define UCLIENT_BASE_TCLIENT 1
#define UCLIENT_BASE_BESTCLIENT 2

#if CONF_UCLIENT_BASE >= UCLIENT_BASE_TCLIENT
#define UCLIENT_HAS_TCLIENT 1
#else
#define UCLIENT_HAS_TCLIENT 0
#endif

#if CONF_UCLIENT_BASE >= UCLIENT_BASE_BESTCLIENT
#define UCLIENT_HAS_BESTCLIENT 1
#else
#define UCLIENT_HAS_BESTCLIENT 0
#endif

inline const char *UClientBaseName()
{
#if UCLIENT_HAS_BESTCLIENT
	return "BestClient";
#elif UCLIENT_HAS_TCLIENT
	return "TClient";
#else
	return "DDNet";
#endif
}

#endif
