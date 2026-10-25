/*
 * Basarabia RP - actualizare automata a launcherului.
 *
 * La pornire (doar procesul principal) citim BRP_SELFUPDATE_URL (version.json de pe site).
 * Daca versiunea de acolo e mai noua decat cea instalata (brp_version.txt, scris de
 * installer langa BasarabiaRP.exe), descarcam installerul nou, il pornim silentios
 * (/VERYSILENT) si inchidem launcherul. Installerul inchide procesele vechi, inlocuieste
 * fisierele si reporneste launcherul singur.
 *
 * Orice eroare (fara internet, site cazut, JSON gresit) => launcherul porneste normal.
 */

#include "StdInc.h"

#ifdef LAUNCHER_PERSONALITY_MAIN
#include <shellapi.h>

#include <fstream>
#include <sstream>
#include <vector>

#include <json.hpp>

#ifdef BRP_SELFUPDATE_URL

static std::wstring BrpExeDir()
{
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(GetModuleHandle(nullptr), exePath, std::size(exePath));

	std::wstring dir = exePath;
	return dir.substr(0, dir.find_last_of(L'\\'));
}

static std::vector<int> BrpParseVersion(const std::string& v)
{
	std::vector<int> parts;
	std::stringstream ss(v);
	std::string item;

	while (std::getline(ss, item, '.'))
	{
		try
		{
			parts.push_back(std::stoi(item));
		}
		catch (...)
		{
			parts.push_back(0);
		}
	}

	while (parts.size() < 4)
	{
		parts.push_back(0);
	}

	return parts;
}

static bool BrpIsNewer(const std::string& remote, const std::string& local)
{
	return BrpParseVersion(remote) > BrpParseVersion(local);
}

static std::string BrpInstalledVersion()
{
	std::ifstream f(BrpExeDir() + L"\\brp_version.txt");
	std::string v;
	std::getline(f, v);

	while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
	{
		v.pop_back();
	}

	return v;
}

// true = launcherul continua normal; false = s-a pornit actualizarea, launcherul se inchide
bool Brp_CheckSelfUpdate()
{
	// developerii pot sari peste actualizare
	if (wcsstr(GetCommandLineW(), L"-brpnoupdate"))
	{
		return true;
	}

	std::string local = BrpInstalledVersion();

	if (local.empty())
	{
		// instalare fara installer (ex. copiat manual) - nu stim versiunea, nu actualizam
		return true;
	}

	static char buffer[8192];

	// "?t=" ca sa nu primim o copie veche din cache
	std::string checkUrl = std::string(BRP_SELFUPDATE_URL) + "?t=" + std::to_string(_time64(nullptr));

	if (DL_RequestURL(checkUrl.c_str(), buffer, sizeof(buffer) - 1) != 0)
	{
		return true;
	}

	std::string remote, url;
	int64_t size = 0;

	try
	{
		auto j = nlohmann::json::parse(buffer);
		remote = j.value("version", "");
		url = j.value("url", "");
		size = j.value("size", int64_t(0));
	}
	catch (...)
	{
		return true;
	}

	if (remote.empty() || url.rfind("https://", 0) != 0 || !BrpIsNewer(remote, local))
	{
		return true;
	}

	char tempDir[MAX_PATH];
	GetTempPathA(std::size(tempDir), tempDir);

	std::string setupPath = std::string(tempDir) + "BasarabiaRP-Update.exe";
	_unlink(setupPath.c_str());

	CL_QueueDownload(url.c_str(), setupPath.c_str(), size, compressionAlgo_e::None);

	UI_DoCreation(true);
	UI_UpdateText(0, ToWide(fmt::sprintf("Se actualizeaza Basarabia RP la versiunea %s...", remote)).c_str());
	UI_UpdateText(1, L"Descarcam versiunea noua. Launcherul reporneste singur la final.");

	bool ok = DL_RunLoop();

	UI_DoDestruction();

	if (!ok || GetFileAttributesA(setupPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		// descarcare esuata sau anulata: pornim versiunea curenta
		return true;
	}

	// installerul: fara ferestre; inchide launcherul vechi si il reporneste la final
	auto result = ShellExecuteW(nullptr, L"open", ToWide(setupPath).c_str(),
		L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /BRPUPDATE", nullptr, SW_SHOWNORMAL);

	if (reinterpret_cast<intptr_t>(result) <= 32)
	{
		return true;
	}

	return false;
}

#else

bool Brp_CheckSelfUpdate()
{
	return true;
}

#endif
#endif
