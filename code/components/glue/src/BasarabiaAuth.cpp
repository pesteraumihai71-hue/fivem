/*
 * Basarabia RP - login propriu (Discord) in loc de contul/biletul Cfx.re.
 *
 * La conectare:
 *  1. daca avem un bilet valid salvat (%LOCALAPPDATA%\Basarabia RP\brp_ticket.txt),
 *     il trimitem serverului ca `brpTicket`;
 *  2. altfel deschidem in browser pagina de login (BRP_AUTH_LOGIN_URL); dupa login
 *     cu Discord, pagina deschide linkul basarabiarp://brp-auth/?t=<bilet>, care
 *     salveaza biletul (vezi ConnectToNative.cpp), iar noi il preluam si continuam.
 */

#include "StdInc.h"

#include <NetLibrary.h>
#include <BasarabiaConfig.h>

#include <ShlObj.h>
#include <shellapi.h>

#include <botan/base64.h>
#include <json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <thread>

using json = nlohmann::json;

static std::wstring BrpTicketPath()
{
	PWSTR appData = nullptr;
	std::wstring path;

	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
	{
		path = appData;
		path += L"\\" BRP_PRODUCT_NAME_W;
		CreateDirectoryW(path.c_str(), nullptr);
		path += L"\\brp_ticket.txt";
	}

	if (appData)
	{
		CoTaskMemFree(appData);
	}

	return path;
}

// folosit si de ConnectToNative.cpp (link basarabiarp://brp-auth/?t=...)
void BrpSaveTicket(const std::string& ticket)
{
	auto path = BrpTicketPath();

	if (path.empty() || ticket.empty() || ticket.size() > 4096)
	{
		return;
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << ticket;
}

static void BrpForgetTicket()
{
	auto path = BrpTicketPath();

	if (!path.empty())
	{
		DeleteFileW(path.c_str());
	}
}

static std::string BrpLoadTicket()
{
	auto path = BrpTicketPath();

	if (path.empty())
	{
		return {};
	}

	std::ifstream in(path, std::ios::binary);
	std::string ticket((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

	// fara spatii/linii noi
	while (!ticket.empty() && (ticket.back() == '\n' || ticket.back() == '\r' || ticket.back() == ' '))
	{
		ticket.pop_back();
	}

	return ticket;
}

// Verificare locala (doar format + expirare); semnatura o verifica serverul.
static bool BrpTicketLooksValid(const std::string& ticket)
{
	auto dot = ticket.find('.');

	if (ticket.empty() || dot == std::string::npos || dot == 0)
	{
		return false;
	}

	try
	{
		std::string b64 = ticket.substr(0, dot);

		for (auto& c : b64)
		{
			if (c == '-') c = '+';
			else if (c == '_') c = '/';
		}

		while (b64.size() % 4 != 0)
		{
			b64 += '=';
		}

		auto decoded = Botan::base64_decode(b64);
		auto payload = json::parse(std::string(decoded.begin(), decoded.end()));

		// marja de 5 minute, ca sa nu expire chiar in timpul conectarii
		return payload.value("e", int64_t(0)) > static_cast<int64_t>(std::time(nullptr)) + 300;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

static InitFunction initFunction([]()
{
	NetLibrary::OnNetLibraryCreate.Connect([](NetLibrary* lib)
	{
		lib->OnInterceptConnectionForAuth.Connect([lib](const std::string& url, const std::string& licenseKeyToken, const std::function<void(bool, const std::map<std::string, std::string>&)>& cb)
		{
			auto ticket = BrpLoadTicket();

			if (BrpTicketLooksValid(ticket))
			{
				cb(true, { { "brpTicket", ticket } });
				return false;
			}

			BrpForgetTicket();

			lib->OnConnectionProgress("Loghează-te cu Discord în browser ca să intri pe " BRP_PRODUCT_NAME_A "...", 0, 100, true);
			ShellExecuteW(nullptr, L"open", BRP_AUTH_LOGIN_URL_W, nullptr, nullptr, SW_SHOWNORMAL);

			std::thread([lib, cb]()
			{
				using namespace std::chrono_literals;

				// asteptam pana la 5 minute biletul salvat de linkul basarabiarp://brp-auth
				for (int i = 0; i < 300; i++)
				{
					std::this_thread::sleep_for(1s);

					auto newTicket = BrpLoadTicket();

					if (BrpTicketLooksValid(newTicket))
					{
						lib->OnConnectionProgress("Autentificat. Se conectează la " BRP_PRODUCT_NAME_A "...", 0, 100, false);
						cb(true, { { "brpTicket", newTicket } });
						return;
					}
				}

				cb(false, {});
				lib->OnConnectionError("Nu te-ai logat cu Discord la timp. Repornește " BRP_PRODUCT_NAME_A " și încearcă din nou.");
			}).detach();

			return false;
		});

		// daca serverul respinge biletul (expirat, secret schimbat etc.), il stergem,
		// ca la urmatoarea pornire sa se ceara login din nou
		lib->OnConnectionErrorEvent.Connect([](const char* error)
		{
			if (error && strstr(error, BRP_PRODUCT_NAME_A))
			{
				if (strstr(error, "Bilet") || strstr(error, "bilet") || strstr(error, "Sesiunea") || strstr(error, "autentificat"))
				{
					BrpForgetTicket();
				}
			}

			return true;
		});
	});
});
