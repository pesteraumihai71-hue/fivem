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
#include <bcrypt.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <cstdio>
#include <vector>
#include <set>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

void UI_DoCreation(bool safeMode);
void UI_DoDestruction();
void UI_UpdateText(int textControl, const wchar_t* text);

namespace
{
	volatile LONG g_monitorStarted = 0;
	volatile LONG g_runtimeAlertShown = 0;
	volatile LONG g_debugSignalLogged = 0;
	volatile LONG g_integrityAlertShown = 0;
	std::wstring g_launcherBaselineHash;
	std::set<std::wstring> g_auditedUnsignedModules;

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
		swprintf_s(line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
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

	// Stage 8: hash the on-disk launcher executable with SHA-256. This detects
	// changes to the file after this process established its baseline; it is not
	// remote attestation and cannot detect every in-memory patch.
	std::wstring BrpHashCurrentExecutable()
	{
		wchar_t exePath[MAX_PATH] = {};
		DWORD pathLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		if (pathLength == 0 || pathLength >= MAX_PATH) return L"";

		HANDLE file = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE) return L"";

		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hashHandle = nullptr;
		std::wstring result;
		DWORD objectLength = 0, hashLength = 0, bytes = 0;
		std::vector<UCHAR> hashObject;
		std::vector<UCHAR> hashBytes;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) goto cleanup;
		if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytes, 0) < 0) goto cleanup;
		if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &bytes, 0) < 0) goto cleanup;
		hashObject.resize(objectLength);
		hashBytes.resize(hashLength);
		if (BCryptCreateHash(algorithm, &hashHandle, hashObject.data(), objectLength, nullptr, 0, 0) < 0) goto cleanup;
		{
			UCHAR buffer[64 * 1024];
			DWORD readCount = 0;
			for (;;)
			{
				if (!ReadFile(file, buffer, sizeof(buffer), &readCount, nullptr)) goto cleanup;
				if (readCount == 0) break;
				if (BCryptHashData(hashHandle, buffer, readCount, 0) < 0) goto cleanup;
			}
		}
		if (BCryptFinishHash(hashHandle, hashBytes.data(), hashLength, 0) < 0) goto cleanup;
		{
			static const wchar_t digits[] = L"0123456789abcdef";
			result.reserve(hashBytes.size() * 2);
			for (UCHAR byte : hashBytes)
			{ result.push_back(digits[byte >> 4]); result.push_back(digits[byte & 0x0f]); }
		}
	cleanup:
		if (hashHandle) BCryptDestroyHash(hashHandle);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		CloseHandle(file);
		return result;
	}

	// Stage 9: audit module signatures in the game process. This is strictly
	// telemetry: unsigned modules can be legitimate (mods, overlays, accessibility
	// tools, or development builds), so this never blocks or terminates anything.
	bool BrpHasValidAuthenticodeSignature(const wchar_t* path)
	{
		if (!path || !*path) return false;
		WINTRUST_FILE_INFO fileInfo{};
		fileInfo.cbStruct = sizeof(fileInfo);
		fileInfo.pcwszFilePath = path;

		WINTRUST_DATA trustData{};
		trustData.cbStruct = sizeof(trustData);
		trustData.dwUIChoice = WTD_UI_NONE;
		trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
		trustData.dwUnionChoice = WTD_CHOICE_FILE;
		trustData.pFile = &fileInfo;
		trustData.dwStateAction = WTD_STATEACTION_VERIFY;
		trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

		GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		const LONG status = WinVerifyTrust(nullptr, &policy, &trustData);
		trustData.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(nullptr, &policy, &trustData);
		return status == ERROR_SUCCESS;
	}

	void BrpAuditGameModuleSignatures()
	{
		HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (processSnapshot == INVALID_HANDLE_VALUE) return;

		PROCESSENTRY32W process{};
		process.dwSize = sizeof(process);
		if (Process32FirstW(processSnapshot, &process))
		{
			do
			{
				if (!BrpIsGameProcess(process.szExeFile)) continue;
				HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process.th32ProcessID);
				if (modules == INVALID_HANDLE_VALUE) continue; // permissions/bitness unknown, not a detection

				MODULEENTRY32W module{};
				module.dwSize = sizeof(module);
				unsigned inspected = 0;
				if (Module32FirstW(modules, &module))
				{
					do
					{
						if (++inspected > 80) break; // bounded work per pass
						if (!BrpHasValidAuthenticodeSignature(module.szExePath))
						{
							const std::wstring key = std::to_wstring(process.th32ProcessID) + L":" + BrpLower(module.szModule);
							if (g_auditedUnsignedModules.insert(key).second)
							{
								// Stage 10: keep enough local context for a human review. Do not
								// upload paths or automatically sanction based on signature status.
								BrpLog(L"Stage 10: modul fără semnătură Authenticode validă; audit-only; PID=" +
									std::to_wstring(process.th32ProcessID) + L"; joc=" + std::wstring(process.szExeFile) +
									L"; modul=" + module.szModule + L"; cale=" + module.szExePath +
									L". Semnătura lipsă nu dovedește cheat; fără sancțiune automată.");
							}
						}
					} while (Module32NextW(modules, &module));
				}
				CloseHandle(modules);
			} while (Process32NextW(processSnapshot, &process));
		}
		CloseHandle(processSnapshot);
	}

	// Stage 7: debugger signals are audit-only. Debuggers can be legitimate
	// developer tools, so this signal is never treated as proof of cheating
	// and never causes an automatic kick/ban. Access-denied is inconclusive.
	bool BrpFindDebuggedGameProcess(std::wstring& gameName)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE) return false;

		PROCESSENTRY32W process{};
		process.dwSize = sizeof(process);
		bool found = false;
		if (Process32FirstW(snapshot, &process))
		{
			do
			{
				if (!BrpIsGameProcess(process.szExeFile)) continue;
				HANDLE target = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
				if (!target) continue; // access denied is not a cheat signal
				BOOL remoteDebugger = FALSE;
				const BOOL querySucceeded = CheckRemoteDebuggerPresent(target, &remoteDebugger);
				CloseHandle(target);
				if (querySucceeded && remoteDebugger)
				{
					gameName = process.szExeFile;
					found = true;
					break;
				}
			} while (Process32NextW(snapshot, &process));
		}
		CloseHandle(snapshot);
		return found;
	}

	DWORD WINAPI BrpRuntimeMonitor(LPVOID)
	{
		ULONGLONG lastIntegrityCheck = 0;
		ULONGLONG lastModuleSignatureAudit = 0;
		// User-mode telemetry only. A module snapshot can fail because of OS
		// permissions or process bitness; such failures are not treated as cheats.
		for (;;)
		{
			const ULONGLONG now = GetTickCount64();
			if (now - lastModuleSignatureAudit >= 60000)
			{
				lastModuleSignatureAudit = now;
				BrpAuditGameModuleSignatures();
			}
			if (now - lastIntegrityCheck >= 30000)
			{
				lastIntegrityCheck = now;
				const std::wstring currentHash = BrpHashCurrentExecutable();
				if (!currentHash.empty() && !g_launcherBaselineHash.empty() && currentHash != g_launcherBaselineHash)
				{
					BrpLog(L"Stage 8: hash-ul executabilului launcher s-a schimbat după pornire. Necesită verificare; nu este dovadă automată de cheat.");
					if (InterlockedCompareExchange(&g_integrityAlertShown, 1, 0) == 0)
						MessageBoxW(nullptr, L"TRKAC a observat o schimbare a fișierului launcherului în timpul rulării. Închide launcherul și descarcă o copie verificată. Dacă problema persistă, contactează staff-ul.", L"Basarabia RP — verificare integritate", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
				}
				else if (currentHash.empty())
					BrpLog(L"Stage 8: hash-ul launcherului nu a putut fi recalculat; rezultat necunoscut, fără sancțiune.");
			}
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
			// Stage 7: audit-only debugger checks. No process is modified or terminated.
			std::wstring debuggedGame;
			if (IsDebuggerPresent() || BrpFindDebuggedGameProcess(debuggedGame))
			{
				if (InterlockedCompareExchange(&g_debugSignalLogged, 1, 0) == 0)
				{
					if (debuggedGame.empty())
						BrpLog(L"Stage 7: semnal de debugger atașat launcherului. Semnal informativ, nu dovadă de cheat.");
					else
						BrpLog(L"Stage 7: semnal de debugger atașat procesului " + debuggedGame + L". Semnal informativ, nu dovadă de cheat.");
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
	g_launcherBaselineHash = BrpHashCurrentExecutable();
	if (!g_launcherBaselineHash.empty())
		BrpLog(L"Stage 8: SHA-256 baseline al executabilului launcher a fost stabilit; verificare periodică activă.");
	else
		BrpLog(L"Stage 8: nu am putut stabili hash-ul launcherului; verificarea de integritate rămâne indisponibilă.");

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

	if (IsDebuggerPresent())
		BrpLog(L"Stage 7: debugger detectat atașat launcherului la preflight. Audit-only; nu se blochează lansarea.");

	BrpLog(L"Preflight initial terminat; monitorul runtime este pornit. Stage 9: audit semnături module joc activ, numai informativ.");
	BrpStartRuntimeMonitor();
	return true;
}

#endif // LAUNCHER_PERSONALITY_MAIN
