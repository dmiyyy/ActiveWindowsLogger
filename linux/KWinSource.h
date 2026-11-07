#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Sources.h"

#include <systemd/sd-bus.h>

namespace awl {

// Active window on KDE Plasma (Wayland and X11).
//
// Wayland doesn't let ordinary clients see other windows, so a small KWin
// script is loaded into the compositor via D-Bus (org.kde.KWin /Scripting).
// The script reports every activation / title change back to us by calling
// io.github.ActiveWindowsLogger.Update on the session bus.
class KWinWindowSource : public WindowSource {
public:
	static constexpr const char* kBusName = "io.github.ActiveWindowsLogger";
	static constexpr const char* kObjectPath = "/io/github/ActiveWindowsLogger";
	static constexpr const char* kInterface = "io.github.ActiveWindowsLogger";

	// `bus` - session bus connection, owned by the caller.
	static std::unique_ptr<KWinWindowSource> create(sd_bus* bus, const std::filesystem::path& runtimeDir,
		bool verbose, std::string& error);
	~KWinWindowSource() override;

	const char* name() const override { return "KWin script"; }
	std::optional<WindowInfo> activeWindow() override { return current_; }
	void maintain() override;

private:
	KWinWindowSource(sd_bus* bus, std::filesystem::path scriptFile, bool verbose);

	bool loadScript(std::string& error);
	void unloadScript();

	static int onUpdate(sd_bus_message* m, void* userdata, sd_bus_error* error);
	static int onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error* error);

	sd_bus* bus_;
	std::filesystem::path scriptFile_;
	bool verbose_;
	sd_bus_slot* objectSlot_ = nullptr;
	sd_bus_slot* matchSlot_ = nullptr;

	std::optional<WindowInfo> current_;
	bool scriptLoaded_ = false;
	bool reloadRequested_ = false;
	std::chrono::steady_clock::time_point lastAttempt_;
};

} // namespace awl
