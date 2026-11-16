#pragma once

#include "pch.h"

namespace motion {

void snap(winrt::Microsoft::UI::Xaml::UIElement const& el);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard swap_in(winrt::Microsoft::UI::Xaml::UIElement const& el);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard soft_in(winrt::Microsoft::UI::Xaml::UIElement const& el);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard pop_in(winrt::Microsoft::UI::Xaml::UIElement const& el);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard slide_in_x(winrt::Microsoft::UI::Xaml::UIElement const& el, double from_x, int ms);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard slide_in_y(winrt::Microsoft::UI::Xaml::UIElement const& el, double from_y, int ms);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard slide_out_y(winrt::Microsoft::UI::Xaml::UIElement const& el, double to_y, int ms);
void press(winrt::Microsoft::UI::Xaml::UIElement const& el, bool down);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard slide_x(winrt::Microsoft::UI::Xaml::Media::TranslateTransform const& shift, double to, int ms, bool animate);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard knob(winrt::Microsoft::UI::Xaml::Shapes::Ellipse const& knob_el, double to, bool animate);
void spin_on(winrt::Microsoft::UI::Xaml::UIElement const& el);
void spin_off(winrt::Microsoft::UI::Xaml::UIElement const& el);
winrt::Microsoft::UI::Xaml::Media::Animation::Storyboard sheet(winrt::Microsoft::UI::Xaml::UIElement const& overlay, winrt::Microsoft::UI::Xaml::Media::ScaleTransform const& scale, double from_opacity, double from_scale, double opacity, double to_scale, int ms);

}
