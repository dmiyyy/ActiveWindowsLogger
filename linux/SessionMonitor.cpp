#include "SessionMonitor.h"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace awl {

SessionMonitor::~SessionMonitor()
{
	for (auto*& slot : slots_)
		slot = sd_bus_slot_unref(slot);
}

void SessionMonitor::attach(sd_bus* systemBus, sd_bus* sessionBus, bool verbose)
{
	verbose_ = verbose;

	if (systemBus) {
		sd_bus_match_signal(systemBus, &slots_[0], "org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "PrepareForSleep", &SessionMonitor::onPrepareForSleep, this);

		// Our session. "auto" also works when we run as a systemd user service
		// (outside of the session): logind returns the user's display session.
		sd_bus_error err = SD_BUS_ERROR_NULL;
		sd_bus_message* reply = nullptr;
		int r = sd_bus_call_method(systemBus, "org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "GetSession", &err, &reply, "s", "auto");
		const char* path = nullptr;
		if (r >= 0 && sd_bus_message_read(reply, "o", &path) >= 0 && path) {
			std::string sessionPath = path;
			sd_bus_match_signal(systemBus, &slots_[1], "org.freedesktop.login1", sessionPath.c_str(),
				"org.freedesktop.DBus.Properties", "PropertiesChanged", &SessionMonitor::onSessionPropertiesChanged,
				this);

			sd_bus_error perr = SD_BUS_ERROR_NULL;
			int locked = 0;
			if (sd_bus_get_property_trivial(systemBus, "org.freedesktop.login1", sessionPath.c_str(),
					"org.freedesktop.login1.Session", "LockedHint", &perr, 'b', &locked) >= 0) {
				lockedHint_ = locked;
			}
			sd_bus_error_free(&perr);
			if (verbose_)
				std::fprintf(stderr, "logind session: %s\n", sessionPath.c_str());
		}
		else if (verbose_) {
			std::fprintf(stderr, "Warning: logind session not found: %s\n",
				err.message ? err.message : std::strerror(-r));
		}
		sd_bus_message_unref(reply);
		sd_bus_error_free(&err);
	}

	if (sessionBus) {
		sd_bus_match_signal(sessionBus, &slots_[2], nullptr, nullptr, "org.freedesktop.ScreenSaver", "ActiveChanged",
			&SessionMonitor::onScreenSaverActiveChanged, this);
		sd_bus_match_signal(sessionBus, &slots_[3], nullptr, nullptr, "org.gnome.ScreenSaver", "ActiveChanged",
			&SessionMonitor::onScreenSaverActiveChanged, this);
	}
}

int SessionMonitor::onPrepareForSleep(sd_bus_message* m, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<SessionMonitor*>(userdata);
	int start = 0;
	if (sd_bus_message_read(m, "b", &start) >= 0) {
		self->sleeping_ = start;
		if (self->verbose_)
			std::fprintf(stderr, start ? "System is going to sleep\n" : "System resumed\n");
	}
	return 0;
}

int SessionMonitor::onSessionPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<SessionMonitor*>(userdata);
	const char* iface = nullptr;
	if (sd_bus_message_read(m, "s", &iface) < 0)
		return 0;
	if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0)
		return 0;
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char* key = nullptr;
		if (sd_bus_message_read(m, "s", &key) < 0)
			return 0;
		if (key && std::strcmp(key, "LockedHint") == 0) {
			int value = 0;
			if (sd_bus_message_read(m, "v", "b", &value) >= 0) {
				self->lockedHint_ = value;
				if (self->verbose_)
					std::fprintf(stderr, value ? "Session locked\n" : "Session unlocked\n");
			}
		}
		else {
			sd_bus_message_skip(m, "v");
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	return 0;
}

int SessionMonitor::onScreenSaverActiveChanged(sd_bus_message* m, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<SessionMonitor*>(userdata);
	int active = 0;
	if (sd_bus_message_read(m, "b", &active) >= 0) {
		self->screenSaverActive_ = active;
		if (self->verbose_)
			std::fprintf(stderr, active ? "Screen saver activated\n" : "Screen saver deactivated\n");
	}
	return 0;
}

} // namespace awl
