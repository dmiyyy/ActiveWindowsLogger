#include "WaylandIdle.h"

#include <poll.h>
#include <wayland-client.h>

#include <cstring>

#include "ext-idle-notify-v1-client-protocol.h"

namespace awl {

namespace {

void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
	auto* self = static_cast<WaylandIdleDetector*>(data);
	if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->seat_) {
		self->seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
	}
	else if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0 && !self->notifier_) {
		self->notifier_ =
			static_cast<ext_idle_notifier_v1*>(wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1));
	}
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {registryGlobal, registryGlobalRemove};

void onIdled(void* data, ext_idle_notification_v1*)
{
	static_cast<WaylandIdleDetector*>(data)->idle_ = true;
}

void onResumed(void* data, ext_idle_notification_v1*)
{
	static_cast<WaylandIdleDetector*>(data)->idle_ = false;
}

const ext_idle_notification_v1_listener kNotificationListener = {onIdled, onResumed};

} // namespace

std::unique_ptr<WaylandIdleDetector> WaylandIdleDetector::create(std::string& error)
{
	std::unique_ptr<WaylandIdleDetector> self(new WaylandIdleDetector());
	self->display_ = wl_display_connect(nullptr);
	if (!self->display_) {
		error = "cannot connect to the Wayland display";
		return nullptr;
	}
	self->registry_ = wl_display_get_registry(self->display_);
	wl_registry_add_listener(self->registry_, &kRegistryListener, self.get());
	wl_display_roundtrip(self->display_);

	if (!self->seat_ || !self->notifier_) {
		error = "the compositor doesn't support ext-idle-notify-v1";
		return nullptr;
	}
	return self;
}

WaylandIdleDetector::~WaylandIdleDetector()
{
	if (notification_)
		ext_idle_notification_v1_destroy(notification_);
	if (notifier_)
		ext_idle_notifier_v1_destroy(notifier_);
	if (seat_)
		wl_seat_destroy(seat_);
	if (registry_)
		wl_registry_destroy(registry_);
	if (display_) {
		wl_display_flush(display_);
		wl_display_disconnect(display_);
	}
}

void WaylandIdleDetector::setThreshold(std::chrono::milliseconds threshold)
{
	if (threshold == threshold_ && (notification_ || threshold.count() <= 0))
		return;
	threshold_ = threshold;
	if (notification_) {
		ext_idle_notification_v1_destroy(notification_);
		notification_ = nullptr;
	}
	idle_ = false;
	if (threshold_.count() > 0) {
		notification_ =
			ext_idle_notifier_v1_get_idle_notification(notifier_, static_cast<uint32_t>(threshold_.count()), seat_);
		ext_idle_notification_v1_add_listener(notification_, &kNotificationListener, this);
	}
	wl_display_flush(display_);
}

void WaylandIdleDetector::dispatch()
{
	// Non-blocking read of pending events.
	while (wl_display_prepare_read(display_) != 0)
		wl_display_dispatch_pending(display_);
	wl_display_flush(display_);

	pollfd pfd{wl_display_get_fd(display_), POLLIN, 0};
	if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
		wl_display_read_events(display_);
	else
		wl_display_cancel_read(display_);

	wl_display_dispatch_pending(display_);
}

bool WaylandIdleDetector::isIdle()
{
	if (!notification_)
		return false;
	dispatch();
	return idle_;
}

} // namespace awl
