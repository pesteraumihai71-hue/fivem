/*
 * This file is part of the Cfx project - https://cfx.re/
 *
 * See LICENSE in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"

#ifdef LAUNCHER_PERSONALITY_MAIN
#include <CfxLocale.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <sstream>
#include <tlhelp32.h>
#include <vector>

#include <CfxState.h>
#include <HostSharedData.h>

#include <citversion.h>

extern std::string GetObjectURL(std::string_view objectHash, std::string_view suffix = "");

static bool Bootstrap_UpdateEXE(int exeSize, const std::string& objectHash)
{
	_unlink("CitizenFX.exe.new");

	const char* fn = "CitizenFX.exe.new";
	CL_QueueDownload(GetObjectURL(objectHash, ".xz").c_str(), fn, exeSize, compressionAlgo_e::XZ);

	UI_DoCreation(true);

	UI_UpdateText(0, fmt::sprintf(gettext(L"Bootstrapping %s..."), PRODUCT_NAME).c_str());

	if (!DL_RunLoop())
	{
		UI_DoDestruction();
		return false;
	}

	UI_DoDestruction();

	wchar_t wfn[512];
	MultiByteToWideChar(CP_ACP, 0, fn, -1, wfn, 512);

	wchar_t exePath[512];
	GetModuleFileName(GetModuleHandle(NULL), exePath, sizeof(exePath) / 2);

	STARTUPINFO startupInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	startupInfo.wShowWindow = SW_HIDE;
	startupInfo.dwFlags |= STARTF_USESHOWWINDOW;

	PROCESS_INFORMATION processInfo;

	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::wstringstream passThroughStream;
	for (auto i = 1; i < argc; i++) {
		passThroughStream << L" " << argv[i];
	}
	std::wstring passThrough = passThroughStream.str();
	CreateProcess(wfn, (LPWSTR)va(L"%s -bootstrap \"%s\"%s", wfn, exePath, passThrough.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);

	return false;
}

bool Install_RunInstallMode();
void Install_RunPostInstall();

bool VerifyViability();

extern void ResetUpdateChannel();

extern bool Brp_CheckSelfUpdate();

// Basarabia RP / TRKAC: verificare la pornirea launcherului.
// Citeste DOAR numele proceselor active (ca in Task Manager) si le compara cu o lista
// explicita de unelte de cheat cunoscute. Nu citeste fisiere, nu citeste memoria altor
// programe, nu trimite nimic in retea si nu ramane pornit in fundal. Daca gaseste o unealta
// de cheat deschisa, afiseaza un mesaj si nu lasa jucatorul sa intre pana o inchide.
static bool BrpRunTrkacStartupScan()
{
	static const wchar_t* blockedProcessNames[] = {
		L"cheatengine.exe", L"cheatengine-x86_64.exe", L"cheatengine-i386.exe", L"cheat engine.exe",
		L"xenos.exe", L"xenos64.exe", L"extreme injector.exe", L"extremeinjector.exe",
		L"gh injector.exe", L"gh injector gui.exe", L"processhacker.exe",
	};

	// dezvoltatorii pot sari peste verificare: launcher pornit cu -brpdev
	if (wcsstr(GetCommandLineW(), L"-brpdev"))
	{
		return true;
	}

	// ecran vizibil la pornire (ca EasyAntiCheat): brand + bara de progres
	UI_DoCreation(true);
	UI_UpdateText(0, BRP_PRODUCT_NAME_W L" \u2014 TRKAC AntiCheat");
	UI_UpdateText(1, L"Se verifica sistemul, te rugam asteapta...");
	for (int i = 0; i <= 100; i += 10)
	{
		UI_UpdateProgress((double)i);
		Sleep(12);
	}

	std::vector<std::wstring> detections;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		// nu putem verifica (ex. permisiuni) -> lasam jucatorul sa intre, nu il blocam degeaba
		UI_DoDestruction();
		return true;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry))
	{
		do
		{
			for (const auto* blockedName : blockedProcessNames)
			{
				if (_wcsicmp(entry.szExeFile, blockedName) == 0)
				{
					detections.emplace_back(entry.szExeFile);
					break;
				}
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);

	UI_DoDestruction();

	if (!detections.empty())
	{
		std::wstring message = L"TRKAC a detectat un program care trebuie inchis inainte de conectare:\n\n";
		for (const auto& name : detections)
		{
			message += L"\u2022 " + name + L"\n";
		}
		message += L"\nInchide programul indicat si porneste din nou launcherul.\nDaca e o greseala, scrie staff-ului pe Discord.";
		MessageBoxW(nullptr, message.c_str(), BRP_PRODUCT_NAME_W L" \u2014 TRKAC AntiCheat", MB_OK | MB_ICONWARNING);
		return false;
	}

	return true;
}

bool Bootstrap_DoBootstrap()
{
	// Basarabia RP: TRKAC verifica procesele active la fiecare pornire
#ifdef GTA_FIVE
	if (!BrpRunTrkacStartupScan())
	{
		return false;
	}
#endif

	// Basarabia RP: actualizare automata a launcherului (version.json de pe site)
	if (!Brp_CheckSelfUpdate())
	{
		return false;
	}

	// first check the bootstrapper version
	char bootstrapVersion[256];

	auto contentHeaders = std::make_shared<HttpHeaderList>();

	auto fetchContent = [&contentHeaders, &bootstrapVersion](const std::string& updateChannel)
	{
		return DL_RequestURL(va(CFX_UPDATER_URL "/heads/" CONTENT_NAME "/%s?time=%lld", updateChannel, _time64(NULL)), bootstrapVersion, sizeof(bootstrapVersion), contentHeaders);
	};

	// Basarabia RP: fără server propriu de update-uri, nu contactăm deloc CDN-ul
	// Cfx.re - altfel clientul și-ar suprascrie fișierele cu FiveM-ul oficial.
#if defined(GTA_FIVE) && !defined(BRP_UPDATER_URL)
	constexpr bool brpNoOnlineUpdate = true;
#else
	constexpr bool brpNoOnlineUpdate = false;
#endif

	int result = brpNoOnlineUpdate ? -1 : fetchContent(GetUpdateChannel());
	bool updateDataValid = true;

	if (result != 0)
	{
		bool recovered = false;

		if (brpNoOnlineUpdate && GetFileAttributes(MakeRelativeCitPath(L"CoreRT.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			UI_DisplayError(BRP_PRODUCT_NAME_W L" nu este instalat corect (lipsește CoreRT.dll). Reinstalați jocul folosind installerul de pe site.");
			return false;
		}

		// if not production, try to recover
		if (!brpNoOnlineUpdate && GetUpdateChannel() != "production")
		{
			int newResult = fetchContent("production");

			if (newResult == 0)
			{
				ResetUpdateChannel();
				recovered = true;
			}
		}

		if (!recovered)
		{
			if (GetFileAttributes(MakeRelativeCitPath(L"CoreRT.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				UI_DisplayError(ToWide(va("An error (%i, %s) occurred while checking the bootstrapper version. Check if " CFX_UPDATER_URL " is available in your web browser.", result, DL_RequestURLError())).c_str());
				return false;
			}

			updateDataValid = false;
		}
	}

	if (updateDataValid)
	{
		int version = std::stoi((*contentHeaders)["x-amz-meta-bootstrap-version"]);
		int exeSize = std::stoi((*contentHeaders)["x-amz-meta-bootstrap-size"]);

		if (version == 0 || exeSize == 0)
		{
			if (GetFileAttributes(MakeRelativeCitPath(L"CoreRT.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				UI_DisplayError(ToWide(va("An error (%i, %s) occurred while checking the bootstrapper version. Check if " CFX_UPDATER_URL " is available in your web browser.", result, DL_RequestURLError())).c_str());
				return false;
			}

			updateDataValid = false;
		}

		if (updateDataValid)
		{
			if (version != BASE_EXE_VERSION && GetFileAttributes(MakeRelativeCitPath(L"nobootstrap.txt").c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				return Bootstrap_UpdateEXE(exeSize, (*contentHeaders)["x-amz-meta-bootstrap-object"]);
			}
		}
	}

	// after self-updating, attempt to run install mode if needed
	if (Install_RunInstallMode())
	{
		return false;
	}

	if (GetFileAttributes(MakeRelativeCitPath(L"CoreRT.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		if (GetFileAttributes(MakeRelativeCitPath(L"GTA5.exe").c_str()) != INVALID_FILE_ATTRIBUTES || GetFileAttributes(MakeRelativeCitPath(L"..\\GTA5.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
		{
			MessageBox(NULL, L"Nu instalați " BRP_PRODUCT_NAME_W L" în folderul GTA V. Folosiți un folder separat (de exemplu C:\\Basarabia RP).", L"O\x448\x438\x431\x43A\x430", MB_OK | MB_ICONSTOP);
			return false;
		}
	}

	static HostSharedData<CfxState> initState("CfxInitState");
	initState->ranPastInstaller = true;

    if (!VerifyViability())
    {
        return false;
    }

	if (updateDataValid)
	{
		bool rv = false;

#ifdef GTA_FIVE
		if (launch::IsSDK())
		{
			rv = Updater_RunUpdate({ CONTENT_NAME, "fxdk-five" });
		}
		else
#endif
		{
			rv = Updater_RunUpdate({ CONTENT_NAME });
		}

		if (rv)
		{
			Install_RunPostInstall();
		}

		return rv;
	}

	return true;
}

void Bootstrap_ReplaceExecutable(const wchar_t* fileName, const std::wstring& passThrough)
{
	wchar_t thisFileName[512];
	GetModuleFileName(GetModuleHandle(NULL), thisFileName, sizeof(thisFileName) / 2);

	// try opening the file
	bool opened = false;
	HANDLE hFile;

	while (!opened)
	{
		hFile = CreateFile(fileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hFile != INVALID_HANDLE_VALUE)
		{
			break;
		}

		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			if (!Install_RunInstallMode())
			{
				UI_DisplayError(L"An 'access denied' error was encountered when updating " PRODUCT_NAME L".");
			}
			else
			{
				MessageBox(NULL, PRODUCT_NAME L" has been installed and can be launched from the shortcut in the Start menu.", PRODUCT_NAME, MB_OK | MB_ICONINFORMATION);
			}

			return;
		}
		else if (error != ERROR_SHARING_VIOLATION)
		{
			UI_DisplayError(va(L"Win32 error %i was encountered when updating " PRODUCT_NAME L".", error));
			return;
		}

		Sleep(50);
	}

	// move the file
	CloseHandle(hFile);
	DeleteFile(va(L"%s.old", fileName));

	if (!MoveFile(fileName, va(L"%s.old", fileName)))
	{
		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			UI_DisplayError(L"An 'access denied' error was encountered when updating " PRODUCT_NAME L" (moving to .old).");
			return;
		}
		else
		{
			UI_DisplayError(va(L"Win32 error %i was encountered when updating " PRODUCT_NAME L" (moving to .old).", error));
			return;
		}
	}

	// copy our lovely file
	if (!CopyFile(thisFileName, fileName, TRUE))
	{
		int error = GetLastError();

		if (error == ERROR_ACCESS_DENIED)
		{
			UI_DisplayError(L"An 'access denied' error was encountered when updating " PRODUCT_NAME L" (copying game executable).");
			return;
		}
		else
		{
			UI_DisplayError(va(L"Win32 error %i was encountered when updating " PRODUCT_NAME L" (copying game executable).", error));
			return;
		}
	}

	DeleteFile(va(L"%s.old", fileName));

	ShellExecute(NULL, L"open", fileName, passThrough.c_str(), L"", SW_SHOWDEFAULT);
}

void Bootstrap_MoveExecutable(const wchar_t* mode)
{
	wchar_t thisFileName[512];
	GetModuleFileName(GetModuleHandle(NULL), thisFileName, sizeof(thisFileName) / 2);

	std::wstring outFileName = fmt::sprintf(L"%s\\CitizenFX_uninstall_%d.exe", _wgetenv(L"temp"), time(NULL));

	if (CopyFile(thisFileName, outFileName.c_str(), FALSE))
	{
		wcsrchr(thisFileName, L'\\')[0] = L'\0';

		STARTUPINFO startupInfo;
		memset(&startupInfo, 0, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo;

		CreateProcess(NULL, const_cast<LPWSTR>(va(L"%s -doUninstall \"%s\"", outFileName, thisFileName)), NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo);

		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
	}
}

void Install_Uninstall(const wchar_t* directory);

bool Bootstrap_RunInit()
{
	int argc;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (argc >= 3)
	{
		if (!_wcsicmp(argv[1], L"-bootstrap"))
		{
			std::wstringstream passThrough(L"");
			for (auto i = 3; i < argc; i++) {
				passThrough << (i > 3 ? L" " : L"") << argv[i];
			}

			Bootstrap_ReplaceExecutable(argv[2], passThrough.str());
			LocalFree(argv);
			return true;
		}

		if (!_wcsicmp(argv[1], L"-uninstall"))
		{
			Bootstrap_MoveExecutable(argv[2]);
			LocalFree(argv);
			return true;
		}

		if (!_wcsicmp(argv[1], L"-doUninstall"))
		{
			Install_Uninstall(argv[2]);
			LocalFree(argv);
			return true;
		}
	}

	LocalFree(argv);

	return false;
}
#endif
