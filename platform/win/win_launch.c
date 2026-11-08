//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_launch.c -- Starts bin\Fairlead.exe, or wakes the window that is already open.
//
//===============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

/*
===============
wWinMain
Process entry.
===============
*/
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
	wchar_t				self[MAX_PATH];
	wchar_t				exe[MAX_PATH];
	wchar_t				*slash;
	STARTUPINFOW		si;
	PROCESS_INFORMATION	pi;
	HANDLE				wake;

	(void)inst;
	(void)prev;
	(void)cmd;
	(void)show;

	if (!GetModuleFileNameW(NULL, self, MAX_PATH)) {
		return 1;
	}

	slash = wcsrchr(self, L'\\');

	if (!slash) {
		return 1;
	}

	*slash = 0;

	if (wcslen(self) + 18 >= MAX_PATH) {
		return 1;
	}

	wcscpy(exe, self);
	wcscat(exe, L"\\bin\\Fairlead.exe");
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	wake = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\Fairlead.Show");

	if (wake) {
		SetEvent(wake);
		CloseHandle(wake);
		return 0;
	}

	if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		return 1;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
