#pragma once

#include "App.g.h"
#include "App.xaml.g.h"

namespace winrt::Fairlead::implementation {

struct App : AppT<App> {
	App();
	void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
	winrt::Fairlead::MainWindow window{ nullptr };
};

}

namespace winrt::Fairlead::factory_implementation {

struct App : AppT<App, implementation::App> {};

}
