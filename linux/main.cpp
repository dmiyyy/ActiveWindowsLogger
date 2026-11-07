// Active windows logger for Linux.
//
// Runs in the background (no GUI) and writes the same CSV logs as the Windows
// version. The running instance is controlled with commands:
//   activewindowslogger pause|resume|toggle|reload|stop|status

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../common/ActivityLogger.h"
#include "../common/Settings.h"
#include "Sources.h"

#ifdef AWL_HAVE_X11
#include "X11Source.h"
#endif
#ifdef AWL_HAVE_SDBUS
#include <systemd/sd-bus.h>
#include "KWinSource.h"
#include "SessionMonitor.h"
#endif
#ifdef AWL_HAVE_WAYLAND
#include "WaylandIdle.h"
#endif

#ifndef AWL_VERSION
#define AWL_VERSION "dev"
#endif

namespace fs = std::filesystem;
using namespace std::chrono;

namespace {

constexpr const char* kAppDir = "ActiveWindowsLogger";
constexpr auto kTickInterval = milliseconds(250);

struct Paths {
	fs::path logsDir;      // ~/.local/share/ActiveWindowsLogger/logs
	fs::path settingsFile; // ~/.config/ActiveWindowsLogger/settings.ini
	fs::path runtimeDir;   // $XDG_RUNTIME_DIR/ActiveWindowsLogger
	fs::path pidFile;
	fs::path stateFile;
};

std::string env(const char* name)
{
	const char* v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

fs::path xdgDir(const char* var, const char* fallbackUnderHome)
{
	std::string v = env(var);
	if (!v.empty() && v[0] == '/')
		return fs::path(v);
	std::string home = env("HOME");
	if (home.empty())
		home = ".";
	return fs::path(home) / fallbackUnderHome;
}

Paths makePaths()
{
	Paths p;
	p.logsDir = xdgDir("XDG_DATA_HOME", ".local/share") / kAppDir / "logs";
	p.settingsFile = xdgDir("XDG_CONFIG_HOME", ".config") / kAppDir / "settings.ini";

	std::string runtime = env("XDG_RUNTIME_DIR");
	if (!runtime.empty())
		p.runtimeDir = fs::path(runtime) / kAppDir;
	else
		p.runtimeDir = fs::temp_directory_path() / (std::string(kAppDir) + "-" + std::to_string(getuid()));
	p.pidFile = p.runtimeDir / "instance.pid";
	p.stateFile = p.runtimeDir / "state";
	return p;
}

bool createDir(const fs::path& dir, bool privateDir = false)
{
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (privateDir)
		chmod(dir.c_str(), 0700);
	return fs::is_directory(dir);
}

bool contains(const std::string& haystack, const char* needle)
{
	return haystack.find(needle) != std::string::npos;
}

void printUsage()
{
	std::printf(
		"Usage: activewindowslogger [options] [command]\n"
		"\n"
		"Logs active windows and the time spent in them to daily CSV files.\n"
		"\n"
		"Commands:\n"
		"  run       start logging (default)\n"
		"  pause     pause the running instance\n"
		"  resume    resume the running instance\n"
		"  toggle    pause or resume the running instance\n"
		"  reload    re-read settings.ini (also done automatically on change)\n"
		"  stop      stop the running instance\n"
		"  status    show the state of the running instance and file locations\n"
		"\n"
		"Options:\n"
		"  --backend=auto|x11|kwin   how to detect the active window (default: auto)\n"
		"  -v, --verbose             print diagnostics to stderr\n"
		"  -h, --help                show this help\n"
		"  --version                 show the version\n");
}

// ---------------------------------------------------------------------------
// Single instance & control commands

long readPid(const Paths& paths)
{
	std::ifstream in(paths.pidFile);
	long pid = 0;
	in >> pid;
	return pid;
}

std::string readState(const Paths& paths)
{
	std::ifstream in(paths.stateFile);
	std::string state;
	std::getline(in, state);
	return state;
}

void writeState(const Paths& paths, const std::string& state)
{
	std::ofstream out(paths.stateFile, std::ios::trunc);
	out << state << "\n";
}

// Returns the PID of the running instance, or 0.
long runningInstance(const Paths& paths)
{
	int fd = open(paths.pidFile.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	// If we can lock the file, nobody holds it: the instance is gone.
	bool running = flock(fd, LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
	close(fd);
	return running ? readPid(paths) : 0;
}

int sendCommand(const Paths& paths, const std::string& command)
{
	long pid = runningInstance(paths);

	if (command == "status") {
		std::printf("State:     %s\n", pid ? readState(paths).c_str() : "not running");
		if (pid)
			std::printf("PID:       %ld\n", pid);
		std::printf("Logs:      %s\n", paths.logsDir.c_str());
		std::printf("Settings:  %s\n", paths.settingsFile.c_str());
		return pid ? 0 : 3;
	}

	if (!pid) {
		std::fprintf(stderr, "activewindowslogger is not running\n");
		return 1;
	}

	int sig = 0;
	if (command == "pause")
		sig = SIGUSR1;
	else if (command == "resume")
		sig = SIGUSR2;
	else if (command == "toggle")
		sig = readState(paths) == "paused" ? SIGUSR2 : SIGUSR1;
	else if (command == "reload")
		sig = SIGHUP;
	else if (command == "stop")
		sig = SIGTERM;

	if (kill(static_cast<pid_t>(pid), sig) != 0) {
		std::fprintf(stderr, "cannot signal process %ld: %s\n", pid, std::strerror(errno));
		return 1;
	}
	if (command == "toggle")
		std::printf("%s\n", sig == SIGUSR1 ? "paused" : "resumed");
	return 0;
}

// ---------------------------------------------------------------------------
// The logger

awl::ActivityLogger* g_logger = nullptr;

void onConnectionLost()
{
	if (g_logger)
		g_logger->close();
}

struct Options {
	std::string backend = "auto";
	bool verbose = false;
};

int run(const Paths& paths, const Options& options)
{
	if (!createDir(paths.runtimeDir, true) || !createDir(paths.logsDir) ||
		!createDir(paths.settingsFile.parent_path())) {
		std::fprintf(stderr, "cannot create directories\n");
		return 1;
	}

	// Single instance: keep an exclusive lock on the pid file while running.
	int lockFd = open(paths.pidFile.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lockFd < 0 || flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
		std::fprintf(stderr, "activewindowslogger is already running\n");
		return 1;
	}
	if (ftruncate(lockFd, 0) == 0) {
		std::string pid = std::to_string(getpid()) + "\n";
		if (write(lockFd, pid.data(), pid.size()) < 0) {
			// not fatal
		}
	}

	std::error_code ec;
	if (!fs::exists(paths.settingsFile, ec))
		awl::writeDefaultSettings(paths.settingsFile);

	awl::Settings settings;
	awl::loadSettings(paths.settingsFile, settings);

	awl::ActivityLogger logger(paths.logsDir);
	logger.setSettings(settings);
	if (!logger.open()) {
		std::fprintf(stderr, "cannot open the log file in %s\n", paths.logsDir.c_str());
		return 1;
	}
	g_logger = &logger;

	// --- Signals (control commands) ---
	sigset_t mask;
	sigemptyset(&mask);
	for (int s : {SIGINT, SIGTERM, SIGHUP, SIGUSR1, SIGUSR2})
		sigaddset(&mask, s);
	sigprocmask(SIG_BLOCK, &mask, nullptr);
	int sigFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	signal(SIGPIPE, SIG_IGN);

	// --- Settings file watcher ---
	int inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotifyFd >= 0)
		inotify_add_watch(inotifyFd, paths.settingsFile.parent_path().c_str(),
			IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);

	// --- Session environment ---
	const std::string sessionType = env("XDG_SESSION_TYPE");
	const std::string desktop = env("XDG_CURRENT_DESKTOP") + ":" + env("DESKTOP_SESSION");
	const bool wayland = !env("WAYLAND_DISPLAY").empty() || sessionType == "wayland";
	const bool kde = contains(desktop, "KDE") || contains(desktop, "plasma") || !env("KDE_FULL_SESSION").empty();

	// --- D-Bus ---
#ifdef AWL_HAVE_SDBUS
	sd_bus* sessionBus = nullptr;
	sd_bus* systemBus = nullptr;
	if (sd_bus_open_user(&sessionBus) < 0) {
		sessionBus = nullptr;
		if (options.verbose)
			std::fprintf(stderr, "Warning: no session D-Bus\n");
	}
	if (sd_bus_open_system(&systemBus) < 0) {
		systemBus = nullptr;
		if (options.verbose)
			std::fprintf(stderr, "Warning: no system D-Bus\n");
	}
	awl::SessionMonitor session;
	session.attach(systemBus, sessionBus, options.verbose);
#endif

	// --- Active window source ---
	std::unique_ptr<awl::WindowSource> source;
	std::string error;
	std::string backend = options.backend;
	if (backend == "auto")
		backend = (wayland && kde) ? "kwin" : "x11";

	if (backend == "kwin") {
#ifdef AWL_HAVE_SDBUS
		if (sessionBus)
			source = awl::KWinWindowSource::create(sessionBus, paths.runtimeDir, options.verbose, error);
		else
			error = "no session D-Bus";
#else
		error = "built without D-Bus (sd-bus) support";
#endif
	}
	else if (backend == "x11") {
		if (wayland && options.backend == "auto")
			std::fprintf(stderr,
				"Warning: Wayland session on an unsupported desktop (%s). Only XWayland windows will be seen.\n",
				desktop.c_str());
#ifdef AWL_HAVE_X11
		source = awl::X11WindowSource::create(error);
		awl::setX11ConnectionLostHandler(&onConnectionLost);
#else
		error = "built without X11 support";
#endif
	}
	else {
		error = "unknown backend '" + backend + "'";
	}

	if (!source) {
		std::fprintf(stderr, "Cannot detect active windows (%s backend): %s\n", backend.c_str(), error.c_str());
		return 1;
	}

	// --- Idle detector ---
	std::unique_ptr<awl::IdleDetector> idle;
	error.clear();
#ifdef AWL_HAVE_WAYLAND
	if (wayland)
		idle = awl::WaylandIdleDetector::create(error);
#endif
#ifdef AWL_HAVE_X11
	if (!idle && !wayland)
		idle = awl::X11IdleDetector::create(error);
#endif
	if (!idle)
		std::fprintf(stderr, "Warning: idle detection is not available%s%s\n", error.empty() ? "" : ": ",
			error.c_str());

	auto applySettings = [&](const awl::Settings& s) {
		logger.setSettings(s);
		if (idle)
			idle->setThreshold(minutes(s.stopLoggingWhenInactiveInterval));
	};
	applySettings(settings);

	if (options.verbose) {
		std::fprintf(stderr, "Window source: %s\n", source->name());
		std::fprintf(stderr, "Idle detector: %s\n", idle ? idle->name() : "none");
		std::fprintf(stderr, "Log file:      %s\n", logger.currentFile().c_str());
		std::fprintf(stderr, "Settings:      %s\n", paths.settingsFile.c_str());
	}

	bool paused = false;
	writeState(paths, "running");

	auto reloadSettings = [&]() {
		awl::Settings s;
		awl::loadSettings(paths.settingsFile, s);
		applySettings(s);
		if (options.verbose)
			std::fprintf(stderr, "Settings reloaded\n");
	};

	// --- Main loop ---
	bool quit = false;
	bool wasSuspended = false;
	auto nextTick = steady_clock::now();
	while (!quit) {
		std::vector<pollfd> fds;
		fds.push_back({sigFd, POLLIN, 0});
		if (inotifyFd >= 0)
			fds.push_back({inotifyFd, POLLIN, 0});
#ifdef AWL_HAVE_SDBUS
		for (sd_bus* bus : {sessionBus, systemBus}) {
			if (bus) {
				int events = sd_bus_get_events(bus);
				fds.push_back({sd_bus_get_fd(bus), static_cast<short>(events < 0 ? POLLIN : events), 0});
			}
		}
#endif
		auto timeout = duration_cast<milliseconds>(nextTick - steady_clock::now()).count();
		if (timeout < 0)
			timeout = 0;
		int r = poll(fds.data(), fds.size(), static_cast<int>(timeout));
		if (r < 0 && errno != EINTR)
			break;

		// Signals
		signalfd_siginfo si;
		while (read(sigFd, &si, sizeof(si)) == sizeof(si)) {
			switch (si.ssi_signo) {
			case SIGUSR1:
				paused = true;
				writeState(paths, "paused");
				break;
			case SIGUSR2:
				paused = false;
				writeState(paths, "running");
				break;
			case SIGHUP:
				reloadSettings();
				break;
			default:
				quit = true;
				break;
			}
		}

		// Settings changes
		if (inotifyFd >= 0) {
			alignas(inotify_event) char buf[4096];
			bool changed = false;
			ssize_t len;
			while ((len = read(inotifyFd, buf, sizeof(buf))) > 0) {
				for (char* p = buf; p < buf + len;) {
					auto* ev = reinterpret_cast<inotify_event*>(p);
					if (ev->len > 0 && paths.settingsFile.filename() == ev->name)
						changed = true;
					p += sizeof(inotify_event) + ev->len;
				}
			}
			if (changed)
				reloadSettings();
		}

#ifdef AWL_HAVE_SDBUS
		for (sd_bus* bus : {sessionBus, systemBus}) {
			if (bus)
				while (sd_bus_process(bus, nullptr) > 0) {
				}
		}
#endif

		auto now = steady_clock::now();
		if (now < nextTick)
			continue;
		nextTick = now + kTickInterval;

		source->maintain();

		bool suspend = paused || (idle && idle->isIdle());
#ifdef AWL_HAVE_SDBUS
		suspend = suspend || session.locked() || session.sleeping();
#endif
		if (suspend)
			logger.suspend(now);
		else
			logger.update(source->activeWindow(), now);

		if (options.verbose && suspend != wasSuspended)
			std::fprintf(stderr, suspend ? "Logging suspended\n" : "Logging resumed\n");
		wasSuspended = suspend;
	}

	logger.close();
	g_logger = nullptr;
	source.reset();
	idle.reset();
#ifdef AWL_HAVE_SDBUS
	if (sessionBus)
		sd_bus_flush_close_unref(sessionBus);
	if (systemBus)
		sd_bus_flush_close_unref(systemBus);
#endif
	std::error_code rmEc;
	fs::remove(paths.stateFile, rmEc);
	close(lockFd);
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	Options options;
	std::string command = "run";

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-h" || arg == "--help") {
			printUsage();
			return 0;
		}
		if (arg == "--version") {
			std::printf("activewindowslogger %s\n", AWL_VERSION);
			return 0;
		}
		if (arg == "-v" || arg == "--verbose") {
			options.verbose = true;
		}
		else if (arg.rfind("--backend=", 0) == 0) {
			options.backend = arg.substr(10);
		}
		else if (arg == "--backend" && i + 1 < argc) {
			options.backend = argv[++i];
		}
		else if (arg == "run" || arg == "pause" || arg == "resume" || arg == "toggle" || arg == "reload" ||
			arg == "stop" || arg == "status") {
			command = arg;
		}
		else {
			std::fprintf(stderr, "Unknown argument: %s\n\n", arg.c_str());
			printUsage();
			return 2;
		}
	}

	Paths paths = makePaths();
	if (command != "run")
		return sendCommand(paths, command);
	return run(paths, options);
}
