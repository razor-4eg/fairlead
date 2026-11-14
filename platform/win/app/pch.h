#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unknwn.h>
#include <hstring.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <knownfolders.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <robuffer.h>

#undef GetCurrentTime

#include <coroutine>
#include <chrono>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Text.h>

struct resume_ui {
	winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{ nullptr };

	bool await_ready() const noexcept {
		return queue && queue.HasThreadAccess();
	}

	void await_suspend(std::coroutine_handle<> handle) const {
		if (!queue || !queue.TryEnqueue([handle] { handle.resume(); })) {
			handle.destroy();
		}
	}

	void await_resume() const noexcept {}
};

#include "MainWindow.xaml.h"
