/*
 * Basarabia RP / TRKAC - user-mode anti-cheat foundation.
 *
 * Implements conservative process-name checks before launch and a periodic
 * monitor for a short list of explicit process/module indicators while the
 * launcher remains alive. This is NOT a kernel anti-cheat and does not claim
 * to detect all cheats. It does not inject into, patch, or terminate arbitrary
 * processes. Runtime findings are logged and shown to the player for review.
 */

#include "StdInc.h"

#ifdef LAUNCHER_PERSONALITY_MAIN

#include <TlHelp32.h>
#include <Softpub.h>
#include <WinTrust.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

void UI_DoCreation(bool safeMode);
void UI_DoDestruction();
void UI_UpdateText(int textControl, const wchar_t* text);

namespace
{
	volatile LONG g_monitorStarted = 0;
	volatile LONG g_runtimeAlertShown = 0;

	std::wstring BrpLower(std::wstring value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
		return value;
	}

	const std::vector<std::wstring>& BrpBlockedProcessNames()
	{
		static const std::vector<std::wstring> names = {
			L"cheatengine.exe",
			L"cheatengine-x86_64.exe",
			L"cheatengine-i386.exe",
			L"extremeinjector.exe",
			L"extreme injector.exe",
			L"gh injector.exe",
			L"ghinjector.exe",
			L"xenos.exe",
			L"xenos64.exe"
		};
		return names;
	}

	// Only exact, high-confidence filenames. Avoid broad words like "hook" or
	// "mod" because they collide with legitimate overlays and accessibility apps.
	const std::vector<std::wstring>& BrpBlockedModuleNames()
	{
		static const std::vector<std::wstring> names = {
			L"cheatengine-i386.dll",
			L"cheatengine-x86_64.dll",
			L"extremeinjector.dll",
			L"extreme injector.dll",
			L"gh injector.dll",
			L"ghinjector.dll",
			L"xenos.dll",
			L"xenos64.dll"
		};
		return names;
	}

	bool BrpContains(const std::vector<std::wstring>& values, const std::wstring& candidate)
	{
		const auto lowered = BrpLower(candidate);
		return std::find(values.begin(), values.end(), lowered) != values.end();
	}

	bool BrpIsBlockedProcess(const std::wstring& name)
	{
		return BrpContains(BrpBlockedProcessNames(), name);
	}

	bool BrpIsGameProcess(const std::wstring& name)
	{
		const auto n = BrpLower(name);
		return n == L"gta5.exe" || n == L"fivem_gtaprocess.exe" ||
			n.find(L"fivem_b") == 0 && n.size() > 18 && n.find(L"_gtaprocess.exe") != std::wstring::npos;
	}

	void BrpLog(const std::wstring& message)
	{
		wchar_t tempPath[MAX_PATH] = {};
		if (!GetTempPathW(MAX_PATH, tempPath)) return;
		std::wstring path = tempPath;
		path += L"TRKAC.log";
		HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return;

		SYSTEMTIME st{};
		GetLocalTime(&st);
		wchar_t line[2048] = {};
		_swprintf_s(line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, message.c_str());
		DWORD written = 0;
		WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
		CloseHandle(file);
	}

	bool BrpFindKnownTool(std::wstring& foundName)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return false;

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		bool found = false;
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (BrpIsBlockedProcess(entry.szExeFile))
				{
					foundName = entry.szExeFile;
					found = true;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return found;
	}

	bool BrpFindKnownGameModule(std::wstring& foundName, std::wstring& gameName)
	{
		HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (processSnapshot == INVALID_HANDLE_VALUE) return false;

		PROCESSENTRY32W process{};
		process.dwSize = sizeof(process);
		bool found = false;
		if (Process32FirstW(processSnapshot, &process))
		{
			do
			{
				if (!BrpIsGameProcess(process.szExeFile)) continue;
				HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process.th32ProcessID);
				if (modules == INVALID_HANDLE_VALUE) continue; // access denied / cross-bitness: no conclusion

				MODULEENTRY32W module{};
				module.dwSize = sizeof(module);
				if (Module32FirstW(modules, &module))
				{
					do
					{
						if (BrpContains(BrpBlockedModuleNames(), module.szModule))
						{
						foundName = module.szModule;
						gameName = process.szExeFile;
						found = true;
						break;
					}
					} while (Module32NextW(modules, &module));
				}
				CloseHandle(modules);
				if (found) break;
			} while (Process32NextW(processSnapshot, &process));
		}
		CloseHandle(processSnapshot);
		return found;
	}

	DWORD WINAPI BrpRuntimeMonitor(LPVOID)
	{
		// User-mode telemetry only. A module snapshot can fail because of OS
		// permissions or process bitness; such failures are not treated as cheats.
		for (;;)
		{
			std::wstring name;
			if (BrpFindKnownTool(name))
			{
				BrpLog(L"Indicator proces detectat in timpul rularii: " + name);
				if (InterlockedCompareExchange(&g_runtimeAlertShown, 1, 0) == 0)
				{
					const std::wstring text = L"TRKAC a identificat un proces din lista de verificare: " + name +
						L"\n\nÎnchide aplicația și contactează staff-ul dacă ai impresia că este o detecție greșită.";
					MessageBoxW(nullptr, text.c_str(), L"Basarabia RP — TRKAC", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
				}
			}

			std::wstring moduleName, gameName;
			if (BrpFindKnownGameModule(moduleName, gameName))
			{
				BrpLog(L"Indicator modul detectat in " + gameName + L": " + moduleName);
				if (InterlockedCompareExchange(&g_runtimeAlertShown, 1, 0) == 0)
				{
					const std::wstring text = L"TRKAC a identificat un modul din lista de verificare în procesul " + gameName +
						L":\n\n" + moduleName + L"\n\nVerificarea nu este o dovadă definitivă de cheat. Dacă este o detecție greșită, contactează staff-ul.";
					MessageBoxW(nullptr, text.c_str(), L"Basarabia RP — TRKAC", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
				}
			}
			Sleep(5000);
		}
		return 0;
	}

	// Stage 5 baseline: inspect the launcher's Authenticode signature. This is
	// deliberately audit-only: local development builds may be unsigned, and
	// a signature check alone is not proof that the process has not been patched.
	bool BrpVerifyCurrentExecutableSignature()
	{
		wchar_t exePath[MAX_PATH] = {};
		DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			BrpLog(L"Stage 5: nu pot determina calea executabilului launcherului.");
			return false;
		}

		WINTRUST_FILE_INFO fileInfo{};
		fileInfo.cbStruct = sizeof(fileInfo);
		fileInfo.pcwszFilePath = exePath;

		WINTRUST_DATA trustData{};
		trustData.cbStruct = sizeof(trustData);
		trustData.dwUIChoice = WTD_UI_NONE;
		trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
		trustData.dwUnionChoice = WTD_CHOICE_FILE;
		trustData.pFile = &fileInfo;
		trustData.dwStateAction = WTD_STATEACTION_VERIFY;
		trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

		GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		LONG status = WinVerifyTrust(nullptr, &policy, &trustData);
		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);

		if (status == ERROR_SUCCESS)
		{
			BrpLog(L"Stage 5: semnatura Authenticode a launcherului este validă.");
			return true;
		}

		BrpLog(L"Stage 5: launcher fără semnătură Authenticode validă (build de dezvoltare sau semnare lipsă). Verificare informativă, fără blocare.");
		return false;
	}

	void BrpStartRuntimeMonitor()
	{
		if (InterlockedCompareExchange(&g_monitorStarted, 1, 0) != 0) return;
		HANDLE thread = CreateThread(nullptr, 0, BrpRuntimeMonitor, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
		else BrpLog(L"Nu s-a putut porni monitorul runtime.");
	}
}

bool Brp_AntiCheatPreflight()
{
	// dezvoltatorii pot sari peste verificare: launcher pornit cu -brpdev
	if (wcsstr(GetCommandLineW(), L"-brpdev")) { return true; }

	UI_DoCreation(true);
	UI_UpdateText(0, L"TRKAC: se verifică semnătura launcherului și procesele active...");
	UI_UpdateText(1, L"Verificare preliminară de securitate. Nu închide launcherul.");
	Sleep(120);

	// Audit-only signature baseline. A release build should be signed by the project's trusted certificate; unsigned development builds continue to run.
	BrpVerifyCurrentExecutableSignature();

	std::wstring foundName;
	const bool blocked = BrpFindKnownTool(foundName);
	UI_DoDestruction();

	if (blocked)
	{
		BrpLog(L"Preflight blocat: proces detectat " + foundName);
		const std::wstring message =
			L"TRKAC a detectat un instrument din lista de verificare:\n\n" + foundName +
			L"\n\nÎnchide aplicația și pornește din nou launcherul Basarabia RP.\n\n"
			L"Dacă această detecție este o eroare, contactează staff-ul pentru verificare.";
		MessageBoxW(nullptr, message.c_str(), L"Basarabia RP — TRKAC", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
		return false;
	}

	BrpLog(L"Preflight initial terminat; monitorul runtime este pornit.");
	BrpStartRuntimeMonitor();
	return true;
}

#endif // LAUNCHER_PERSONALITY_MAIN
