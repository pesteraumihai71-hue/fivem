/*
 * Basarabia RP - autentificare proprie (fara Cfx.re).
 *
 * Cand `brp_authSecret` este setat in server.cfg:
 *  - serverul NU mai cere biletul Cfx (cfxTicket2) la conectare;
 *  - clientul Basarabia RP trimite `brpTicket`, emis de resursa `brp_auth`
 *    dupa login cu Discord;
 *  - acest provider verifica semnatura si data expirarii si ii da jucatorului
 *    identificatorul `discord:<id>` (folosit de vRP 2 pentru a-i gasi contul).
 *
 * Format bilet:  base64url(json) "." hex(HMAC-SHA256(brp_authSecret, base64url(json)))
 *   json = { "v": 1, "d": "<discord id>", "u": "<nume>", "e": <expirare unix> }
 */

#include "StdInc.h"
#include <ServerIdentityProvider.h>
#include <ServerInstanceBase.h>

#include <botan/mac.h>
#include <botan/hex.h>
#include <botan/base64.h>

#include <json.hpp>

#include <ctime>

using json = nlohmann::json;

std::shared_ptr<ConVar<std::string>> g_brpAuthSecret;

namespace
{
	bool ConstantTimeEquals(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}

		unsigned char diff = 0;

		for (size_t i = 0; i < a.size(); i++)
		{
			diff |= static_cast<unsigned char>(a[i] ^ b[i]);
		}

		return diff == 0;
	}

	std::string Base64UrlToStandard(std::string s)
	{
		for (auto& c : s)
		{
			if (c == '-') c = '+';
			else if (c == '_') c = '/';
		}

		while (s.size() % 4 != 0)
		{
			s += '=';
		}

		return s;
	}

	bool IsDigits(const std::string& s)
	{
		if (s.empty() || s.size() > 24)
		{
			return false;
		}

		for (char c : s)
		{
			if (c < '0' || c > '9')
			{
				return false;
			}
		}

		return true;
	}

	// Returneaza ID-ul de Discord daca biletul e valid; altfel mesajul de eroare in `error`.
	boost::optional<std::string> VerifyBrpTicket(const std::string& secret, const std::string& ticket, std::string& error)
	{
		auto dot = ticket.find('.');

		if (dot == std::string::npos || dot == 0 || dot + 1 >= ticket.size() || ticket.size() > 4096)
		{
			error = "Biletul Basarabia RP este invalid.";
			return {};
		}

		const std::string payloadB64 = ticket.substr(0, dot);
		const std::string signature = ticket.substr(dot + 1);

		auto mac = Botan::MessageAuthenticationCode::create("HMAC(SHA-256)");

		if (!mac)
		{
			error = "Eroare interna la verificarea biletului (HMAC indisponibil).";
			return {};
		}

		mac->set_key(reinterpret_cast<const uint8_t*>(secret.data()), secret.size());
		mac->update(reinterpret_cast<const uint8_t*>(payloadB64.data()), payloadB64.size());

		const std::string expected = Botan::hex_encode(mac->final(), false);

		std::string signatureLower = signature;
		for (auto& c : signatureLower)
		{
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}

		if (!ConstantTimeEquals(expected, signatureLower))
		{
			error = "Biletul Basarabia RP nu este valid. Loghează-te din nou cu Discord.";
			return {};
		}

		try
		{
			auto decoded = Botan::base64_decode(Base64UrlToStandard(payloadB64));
			auto payload = json::parse(std::string(decoded.begin(), decoded.end()));

			if (payload.value("v", 0) != 1)
			{
				error = "Versiune de bilet necunoscută. Actualizează aplicația Basarabia RP.";
				return {};
			}

			const std::string discordId = payload.value("d", "");
			const int64_t expiry = payload.value("e", int64_t(0));

			if (!IsDigits(discordId))
			{
				error = "Biletul Basarabia RP nu conține un cont Discord valid.";
				return {};
			}

			if (expiry < static_cast<int64_t>(std::time(nullptr)))
			{
				error = "Sesiunea Basarabia RP a expirat. Repornește aplicația și loghează-te din nou cu Discord.";
				return {};
			}

			return discordId;
		}
		catch (const std::exception&)
		{
			error = "Biletul Basarabia RP nu a putut fi citit.";
			return {};
		}
	}
}

static InitFunction initFunction([]()
{
	static struct BasarabiaIdProvider : public fx::ServerIdentityProviderBase
	{
		virtual std::string GetIdentifierPrefix() override
		{
			return "discord";
		}

		virtual int GetVarianceLevel() override
		{
			return 1;
		}

		virtual int GetTrustLevel() override
		{
			return 5;
		}

		virtual void RunAuthentication(const fx::ClientSharedPtr& clientPtr, const std::map<std::string, std::string>& postMap, const std::function<void(boost::optional<std::string>)>& cb) override
		{
			const std::string secret = g_brpAuthSecret ? g_brpAuthSecret->GetValue() : "";

			// autentificarea proprie nu e activata: comportament original
			if (secret.empty())
			{
				cb({});
				return;
			}

			auto it = postMap.find("brpTicket");

			if (it == postMap.end() || it->second.empty())
			{
				cb(std::string{ "Nu ești autentificat în Basarabia RP. Folosește aplicația Basarabia RP și loghează-te cu Discord." });
				return;
			}

			std::string error;
			auto discordId = VerifyBrpTicket(secret, it->second, error);

			if (!discordId)
			{
				cb(error);
				return;
			}

			clientPtr->AddIdentifier(fmt::sprintf("discord:%s", *discordId));
			cb({});
		}
	} idp;

	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		// NU are ConVar_ServerInfo: secretul nu trebuie sa fie public.
		g_brpAuthSecret = instance->AddVariable<std::string>("brp_authSecret", ConVar_None, "");
	});

	fx::RegisterServerIdentityProvider(&idp);
}, 153);
