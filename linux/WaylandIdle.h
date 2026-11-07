#pragma once

#include <memory>
#include <string>

#include "Sources.h"

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_idle_notifier_v1;
struct ext_idle_notification_v1;

namespace awl {

// Idle detection on Wayland via the ext-idle-notify-v1 protocol
// (supported by KWin 6, Sway, Hyprland, Mutter 47+, ...).
// Idle inhibitors (e.g. a playing video) keep the user "active".
class WaylandIdleDetector : public IdleDetector {
public:
	static std::unique_ptr<WaylandIdleDetector> create(std::string& error);
	~WaylandIdleDetector() override;

	const char* name() const override { return "Wayland (ext-idle-notify-v1)"; }
	void setThreshold(std::chrono::milliseconds threshold) override;
	bool isIdle() override;

	// Used by the protocol listeners.
	wl_seat* seat_ = nullptr;
	ext_idle_notifier_v1* notifier_ = nullptr;
	bool idle_ = false;

private:
	WaylandIdleDetector() = default;
	void dispatch();

	wl_display* display_ = nullptr;
	wl_registry* registry_ = nullptr;
	ext_idle_notification_v1* notification_ = nullptr;
	std::chrono::milliseconds threshold_{0};
};

} // namespace awl
