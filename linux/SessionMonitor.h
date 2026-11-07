#pragma once

#include <string>

#include <systemd/sd-bus.h>

namespace awl {

// Watches for screen lock and system sleep:
//  - systemd-logind (system bus): PrepareForSleep, session LockedHint;
//  - org.freedesktop.ScreenSaver / org.gnome.ScreenSaver (session bus): ActiveChanged.
class SessionMonitor {
public:
	SessionMonitor() = default;
	~SessionMonitor();

	SessionMonitor(const SessionMonitor&) = delete;
	SessionMonitor& operator=(const SessionMonitor&) = delete;

	// Both buses are optional (nullptr) and owned by the caller.
	void attach(sd_bus* systemBus, sd_bus* sessionBus, bool verbose);

	bool locked() const { return lockedHint_ || screenSaverActive_; }
	bool sleeping() const { return sleeping_; }

private:
	static int onPrepareForSleep(sd_bus_message* m, void* userdata, sd_bus_error*);
	static int onSessionPropertiesChanged(sd_bus_message* m, void* userdata, sd_bus_error*);
	static int onScreenSaverActiveChanged(sd_bus_message* m, void* userdata, sd_bus_error*);

	bool verbose_ = false;
	bool lockedHint_ = false;
	bool screenSaverActive_ = false;
	bool sleeping_ = false;
	sd_bus_slot* slots_[4] = {};
};

} // namespace awl
