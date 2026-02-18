#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// Simple file logger. Call Logger::instance().init(path) once at startup.
// The log file is truncated on every run.
//
// Usage:
//   LOG_INFO("Loaded %zu meshes", count);
//   LOG_WARN("meshIndex %u out of range (have %zu meshes)", idx, sz);
//   LOG_ERROR("Failed to open file: %s", path.c_str());

class Logger
{
   public:
	static Logger& instance()
	{
		static Logger inst;
		return inst;
	}

	void init_with_path(const std::string& path)
	{
		try
		{
			init(path);
		}
		catch (const std::exception& e)
		{
			std::fprintf(
				stderr,
				"[ERROR] Failed to initialize logger with path '%s': %s\n",
				path.c_str(), e.what());
			init();	 // Fallback to default path
		}
	}

	// Initialize logger to a central, cross-platform per-user location.
	// Falls back to the current working directory if nothing suitable is found.
	void init()
	{
		try
		{
			auto p = get_default_log_path();
			init(p.string());
		}
		catch (const std::exception&)
		{
			// Fallback: use local file name in current working directory
			init(std::string("vulkanwork.log"));
		}
	}

	~Logger()
	{
		if (file_) std::fclose(file_);
	}

	void log(const char* level, const char* fmt, ...)
	{
		char buf[2048];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);

		if (file_)
		{
			std::fprintf(file_, "[%s] %s\n", level, buf);
			std::fflush(file_);
		}
		std::fprintf(stderr, "[%s] %s\n", level, buf);
	}

   private:
	// Determine a sensible default per-user location for the log file.
	// This checks common environment variables and falls back to the system
	// temporary directory when needed. It will also create the parent
	// directories if they don't exist.
	static std::filesystem::path get_default_log_path()
	{
		std::filesystem::path base;
#ifdef _WIN32
		if (const char* p = std::getenv("USERPROFILE"))
			base = p;
		else if (const char* p = std::getenv("LOCALAPPDATA"))
			base = p;
		else if (const char* p = std::getenv("APPDATA"))
			base = p;
		else
			base = std::filesystem::temp_directory_path();
		base /= "VulkanWork";
		base /= "vulkanwork.log";
#else
		if (const char* p = std::getenv("XDG_STATE_HOME"))
			base = p;
		else if (const char* p = std::getenv("XDG_DATA_HOME"))
			base = p;
		else if (const char* p = std::getenv("HOME"))
			base = std::filesystem::path(p) / ".local" / "state";
		else
			base = std::filesystem::temp_directory_path();
		base /= "vulkanwork";
		base /= "vulkanwork.log";
#endif
		try
		{
			std::filesystem::create_directories(base.parent_path());
		}
		catch (...)
		{
		}
		return base;
	}

	void init(const std::string& path)
	{
		if (file_) std::fclose(file_);
		file_ = std::fopen(path.c_str(), "w");
		if (!file_)
			std::fprintf(stderr, "[WARN] Could not open log file: %s\n",
						 path.c_str());
		else
			std::fprintf(stderr, "[INFO] Logging to file: %s\n", path.c_str());
	}

	Logger() = default;
	FILE* file_ = nullptr;
};

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define LOG_INFO(fmt, ...) Logger::instance().log("INFO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) Logger::instance().log("WARN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::instance().log("ERROR", fmt, ##__VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
