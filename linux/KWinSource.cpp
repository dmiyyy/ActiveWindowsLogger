#include "KWinSource.h"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace awl {

namespace {

constexpr const char* kPluginName = "activewindowslogger";

// The KWin script. Supports both the KWin 6 (windowActivated/activeWindow)
// and KWin 5 (clientActivated/activeClient) APIs.
// All arguments are passed as strings: JS numbers would arrive as doubles.
const char* kScript = R"JS(
const SERVICE = "io.github.ActiveWindowsLogger";
const PATH = "/io/github/ActiveWindowsLogger";
const IFACE = "io.github.ActiveWindowsLogger";

let tracked = null;

function report() {
    const w = tracked;
    if (w) {
        callDBus(SERVICE, PATH, IFACE, "Update",
                 String(w.caption || ""), String(w.resourceClass || ""),
                 String(w.resourceName || ""), String(w.pid || 0));
    } else {
        callDBus(SERVICE, PATH, IFACE, "Update", "", "", "", "0");
    }
}

function activated(w) {
    if (tracked && tracked.captionChanged) {
        try { tracked.captionChanged.disconnect(report); } catch (e) {}
    }
    tracked = w || null;
    if (tracked && tracked.captionChanged) {
        tracked.captionChanged.connect(report);
    }
    report();
}

if (workspace.windowActivated) {
    workspace.windowActivated.connect(activated);
    activated(workspace.activeWindow);
} else {
    workspace.clientActivated.connect(activated);
    activated(workspace.activeClient);
}
)JS";

} // namespace

std::unique_ptr<KWinWindowSource> KWinWindowSource::create(sd_bus* bus, const fs::path& runtimeDir, bool verbose,
	std::string& error)
{
	std::unique_ptr<KWinWindowSource> source(
		new KWinWindowSource(bus, runtimeDir / "kwin-active-window.js", verbose));

	// Update(caption, resourceClass, resourceName, pid) - called by the KWin script.
	static const sd_bus_vtable vtable[] = {
		SD_BUS_VTABLE_START(0),
		SD_BUS_METHOD("Update", "ssss", "", &KWinWindowSource::onUpdate, SD_BUS_VTABLE_UNPRIVILEGED),
		SD_BUS_VTABLE_END,
	};

	int r = sd_bus_add_object_vtable(bus, &source->objectSlot_, kObjectPath, kInterface, vtable, source.get());
	if (r < 0) {
		error = std::string("cannot register D-Bus object: ") + std::strerror(-r);
		return nullptr;
	}

	r = sd_bus_request_name(bus, kBusName, 0);
	if (r < 0) {
		error = std::string("cannot acquire D-Bus name ") + kBusName + ": " + std::strerror(-r);
		return nullptr;
	}

	// Reload the script when KWin restarts.
	r = sd_bus_match_signal(bus, &source->matchSlot_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameOwnerChanged", &KWinWindowSource::onNameOwnerChanged, source.get());
	if (r < 0) {
		error = std::string("cannot subscribe to NameOwnerChanged: ") + std::strerror(-r);
		return nullptr;
	}

	std::string loadError;
	if (!source->loadScript(loadError)) {
		// KWin may not be ready yet (e.g. we were started early by systemd); maintain() retries.
		std::fprintf(stderr, "Warning: %s, will retry\n", loadError.c_str());
	}
	return source;
}

KWinWindowSource::KWinWindowSource(sd_bus* bus, fs::path scriptFile, bool verbose)
	: bus_(bus)
	, scriptFile_(std::move(scriptFile))
	, verbose_(verbose)
{
}

KWinWindowSource::~KWinWindowSource()
{
	unloadScript();
	sd_bus_slot_unref(matchSlot_);
	sd_bus_slot_unref(objectSlot_);
	std::error_code ec;
	fs::remove(scriptFile_, ec);
}

bool KWinWindowSource::loadScript(std::string& error)
{
	lastAttempt_ = std::chrono::steady_clock::now();
	scriptLoaded_ = false;

	{
		std::ofstream out(scriptFile_, std::ios::trunc);
		out << kScript;
		if (!out) {
			error = "cannot write " + scriptFile_.string();
			return false;
		}
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = nullptr;

	// Unload a stale copy left by a previous (crashed) instance.
	sd_bus_call_method(bus_, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", &err, &reply,
		"s", kPluginName);
	sd_bus_message_unref(reply);
	reply = nullptr;
	sd_bus_error_free(&err);

	int r = sd_bus_call_method(bus_, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "loadScript", &err,
		&reply, "ss", scriptFile_.c_str(), kPluginName);
	if (r < 0) {
		error = std::string("cannot load KWin script: ") + (err.message ? err.message : std::strerror(-r));
		sd_bus_error_free(&err);
		return false;
	}
	int32_t id = -1;
	sd_bus_message_read(reply, "i", &id);
	sd_bus_message_unref(reply);
	reply = nullptr;
	if (id < 0) {
		error = "KWin refused to load the script";
		return false;
	}

	// Starts all loaded scripts that are not running yet (KWin 5 and 6).
	r = sd_bus_call_method(bus_, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "start", &err, &reply, "");
	sd_bus_message_unref(reply);
	if (r < 0) {
		error = std::string("cannot start KWin script: ") + (err.message ? err.message : std::strerror(-r));
		sd_bus_error_free(&err);
		return false;
	}

	scriptLoaded_ = true;
	if (verbose_)
		std::fprintf(stderr, "KWin script loaded (id %d)\n", id);
	return true;
}

void KWinWindowSource::unloadScript()
{
	if (!scriptLoaded_)
		return;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = nullptr;
	sd_bus_call_method(bus_, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", &err, &reply,
		"s", kPluginName);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	scriptLoaded_ = false;
}

void KWinWindowSource::maintain()
{
	auto now = std::chrono::steady_clock::now();
	if (reloadRequested_ || (!scriptLoaded_ && now - lastAttempt_ >= std::chrono::seconds(5))) {
		reloadRequested_ = false;
		std::string error;
		if (!loadScript(error) && verbose_)
			std::fprintf(stderr, "Warning: %s\n", error.c_str());
	}
}

int KWinWindowSource::onUpdate(sd_bus_message* m, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<KWinWindowSource*>(userdata);
	const char *caption = nullptr, *resourceClass = nullptr, *resourceName = nullptr, *pid = nullptr;
	int r = sd_bus_message_read(m, "ssss", &caption, &resourceClass, &resourceName, &pid);
	if (r < 0)
		return r;

	if ((!caption || !*caption) && (!resourceClass || !*resourceClass) && (!pid || std::strcmp(pid, "0") == 0)) {
		self->current_.reset();
	}
	else {
		WindowInfo info;
		info.title = caption ? caption : "";
		info.windowClass = resourceClass && *resourceClass ? resourceClass : (resourceName ? resourceName : "");
		long p = pid ? std::strtol(pid, nullptr, 10) : 0;
		info.program = programNameForPid(p, info.windowClass);
		self->current_ = std::move(info);
	}

	if (self->verbose_) {
		if (self->current_)
			std::fprintf(stderr, "Active window: [%s] %s\n", self->current_->program.c_str(),
				self->current_->title.c_str());
		else
			std::fprintf(stderr, "Active window: none\n");
	}
	return sd_bus_reply_method_return(m, "");
}

int KWinWindowSource::onNameOwnerChanged(sd_bus_message* m, void* userdata, sd_bus_error*)
{
	auto* self = static_cast<KWinWindowSource*>(userdata);
	const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
	if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || !name)
		return 0;
	if (std::strcmp(name, "org.kde.KWin") != 0)
		return 0;

	self->current_.reset();
	self->scriptLoaded_ = false;
	if (newOwner && *newOwner) {
		// KWin (re)started. Load the script from maintain(), outside of the callback.
		self->reloadRequested_ = true;
	}
	return 0;
}

} // namespace awl
