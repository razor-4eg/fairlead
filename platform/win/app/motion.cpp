#include "pch.h"
#include "motion.h"

namespace motion {
namespace {

namespace mux = winrt::Microsoft::UI::Xaml;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace anim = winrt::Microsoft::UI::Xaml::Media::Animation;
namespace num = winrt::Windows::Foundation::Numerics;

media::TranslateTransform translate_of(mux::UIElement const& el) {
	if (auto existing = el.RenderTransform().try_as<media::TranslateTransform>()) {
		return existing;
	}

	media::TranslateTransform shift;
	el.RenderTransform(shift);
	return shift;
}

media::ScaleTransform scale_of(mux::UIElement const& el) {
	if (auto existing = el.RenderTransform().try_as<media::ScaleTransform>()) {
		return existing;
	}

	media::ScaleTransform scale;
	scale.ScaleX(1);
	scale.ScaleY(1);
	el.RenderTransform(scale);
	return scale;
}

mux::Duration duration_of(int ms) {
	return { std::chrono::milliseconds(ms), mux::DurationType::TimeSpan };
}

anim::DoubleAnimation scalar(mux::DependencyObject const& target, wchar_t const* property, double from, double to, int ms) {
	anim::DoubleAnimation value;
	value.From(winrt::box_value(from).as<winrt::Windows::Foundation::IReference<double>>());
	value.To(to);
	value.Duration(duration_of(ms));
	anim::CubicEase ease;
	ease.EasingMode(anim::EasingMode::EaseOut);
	value.EasingFunction(ease);
	value.EnableDependentAnimation(true);
	anim::Storyboard::SetTarget(value, target);
	anim::Storyboard::SetTargetProperty(value, property);
	return value;
}

anim::Storyboard board_of(int ms) {
	anim::Storyboard board;
	board.Duration(duration_of(ms));
	board.FillBehavior(anim::FillBehavior::HoldEnd);
	return board;
}

anim::Storyboard fade_shift(mux::UIElement const& el, double opacity_from, double opacity_to, double y_from, double y_to, int ms) {
	auto shift = translate_of(el);
	auto board = board_of(ms);
	board.Children().Append(scalar(el, L"Opacity", opacity_from, opacity_to, ms));
	board.Children().Append(scalar(shift, L"Y", y_from, y_to, ms));
	return board;
}

}

void snap(mux::UIElement const& el) {
	el.Opacity(1);
	auto transform = el.RenderTransform();
	if (auto shift = transform.try_as<media::TranslateTransform>()) {
		shift.X(0);
		shift.Y(0);
	} else if (auto scale = transform.try_as<media::ScaleTransform>()) {
		scale.ScaleX(1);
		scale.ScaleY(1);
	}
}

anim::Storyboard swap_in(mux::UIElement const& el) {
	auto shift = translate_of(el);
	shift.Y(8);
	el.Opacity(0);
	return fade_shift(el, 0, 1, 8, 0, 240);
}

anim::Storyboard soft_in(mux::UIElement const& el) {
	auto shift = translate_of(el);
	shift.Y(10);
	el.Opacity(0);
	return fade_shift(el, 0, 1, 10, 0, 320);
}

anim::Storyboard pop_in(mux::UIElement const& el) {
	el.RenderTransformOrigin({ 0.5, 0.5 });
	auto scale = scale_of(el);
	scale.ScaleX(0.9);
	scale.ScaleY(0.9);
	el.Opacity(0);
	auto board = board_of(280);
	board.Children().Append(scalar(el, L"Opacity", 0, 1, 220));
	board.Children().Append(scalar(scale, L"ScaleX", 0.9, 1, 280));
	board.Children().Append(scalar(scale, L"ScaleY", 0.9, 1, 280));
	return board;
}

anim::Storyboard slide_in_x(mux::UIElement const& el, double from_x, int ms) {
	auto shift = translate_of(el);
	shift.X(from_x);
	el.Opacity(0);
	auto board = board_of(ms);
	board.Children().Append(scalar(el, L"Opacity", 0, 1, ms));
	board.Children().Append(scalar(shift, L"X", from_x, 0, ms));
	return board;
}

void press(mux::UIElement const& el, bool down) {
	if (el.ActualSize().x <= 0 || el.ActualSize().y <= 0) {
		return;
	}

	auto visual = mux::Hosting::ElementCompositionPreview::GetElementVisual(el);
	auto compositor = visual.Compositor();
	visual.CenterPoint({ el.ActualSize().x / 2.f, el.ActualSize().y / 2.f, 0 });
	float to = down ? 0.94f : 1.f;
	auto spring = compositor.CreateSpringVector3Animation();
	spring.FinalValue(num::float3{ to, to, 1.f });
	spring.DampingRatio(0.7f);
	spring.Period(std::chrono::milliseconds(40));
	visual.StartAnimation(L"Scale", spring);
}

anim::Storyboard slide_x(media::TranslateTransform const& shift, double to, int ms, bool animate) {
	if (!animate || std::abs(shift.X() - to) < 0.1) {
		shift.X(to);
		return nullptr;
	}

	auto board = board_of(ms);
	board.Children().Append(scalar(shift, L"X", shift.X(), to, ms));
	return board;
}

anim::Storyboard knob(mux::Shapes::Ellipse const& knob_el, double to, bool animate) {
	auto shift = knob_el.RenderTransform().try_as<media::TranslateTransform>();
	if (!shift) {
		return nullptr;
	}

	if (!animate || std::abs(shift.X() - to) < 0.1) {
		shift.X(to);
		return nullptr;
	}

	auto board = board_of(200);
	board.Children().Append(scalar(shift, L"X", shift.X(), to, 200));
	return board;
}

anim::Storyboard spin(media::RotateTransform const& rotate) {
	anim::DoubleAnimation value;
	value.From(winrt::box_value(0.0).as<winrt::Windows::Foundation::IReference<double>>());
	value.To(360);
	value.Duration(duration_of(900));
	value.RepeatBehavior(anim::RepeatBehaviorHelper::Forever());
	value.EnableDependentAnimation(true);
	anim::Storyboard::SetTarget(value, rotate);
	anim::Storyboard::SetTargetProperty(value, L"Angle");
	anim::Storyboard board;
	board.Children().Append(value);
	return board;
}

anim::Storyboard sheet(mux::UIElement const& overlay, media::ScaleTransform const& scale, double from_opacity, double from_scale, double opacity, double to_scale, int ms) {
	auto board = board_of(ms);
	board.Children().Append(scalar(overlay, L"Opacity", from_opacity, opacity, ms));
	board.Children().Append(scalar(scale, L"ScaleX", from_scale, to_scale, ms));
	board.Children().Append(scalar(scale, L"ScaleY", from_scale, to_scale, ms));
	return board;
}

}
