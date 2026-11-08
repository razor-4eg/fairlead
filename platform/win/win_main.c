//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_main.c -- Entry for fairlead-helper.exe. The window is the WinUI process beside this binary.
//
//===============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

int helper_main(const char *token, const char *exe_dir);

/*
===============
wWinMain
Process entry.
===============
*/
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
	int		argc;
	wchar_t	**argv;
	char	token[80];
	char	dir[MAX_PATH];
	wchar_t	exe[MAX_PATH];
	wchar_t	*slash;

	argc = 0;

	(void)inst;
	(void)prev;
	(void)cmd;
	(void)show;

	argv = CommandLineToArgvW(GetCommandLineW(), &argc);

	if (argc < 3 || wcscmp(argv[1], L"--helper") != 0) {
		LocalFree(argv);
		return 1;
	}

	WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, token, 80, NULL, NULL);
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	slash = wcsrchr(exe, L'\\');

	if (slash) {
		*slash = 0;
	}

	WideCharToMultiByte(CP_UTF8, 0, exe, -1, dir, MAX_PATH, NULL, NULL);
	LocalFree(argv);

	return helper_main(token, dir);
}
