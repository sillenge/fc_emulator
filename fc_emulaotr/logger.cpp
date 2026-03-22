#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <filesystem>

#include "Logger.h"

namespace fc_emulator {

// 设置是否在控制台显示
void Logger::setShowConsole(bool show) {
    showConsole_ = show;
}

// 设置是否显示时间戳
void Logger::setShowTimestamp(bool show) {
    showTimestamp_ = show;
}

// 不同级别的日志方法
void Logger::debug(const std::string& message, const std::string& file, int line) {
    logInternal(LogLevel::LevelDebug, message, file, line);
}

void Logger::info(const std::string& message, const std::string& file, int line) {
    logInternal(LogLevel::LevelInfo, message, file, line);
}

void Logger::warning(const std::string& message, const std::string& file, int line) {
    logInternal(LogLevel::LevelWarning, message, file, line);
}

void Logger::error(const std::string& message, const std::string& file, int line) {
    logInternal(LogLevel::LevelError, message, file, line);
}

void Logger::fatal(const std::string& message, const std::string& file, int line) {
    logInternal(LogLevel::LevelFatal, message, file, line);
}

// 获取当前日志文件名
std::string Logger::getLogFileName() const {
    return logFileName_;
}

// 重新打开日志文件
void Logger::reopenLogFile() {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
    openLogFile();
}

// 构造函数
Logger::Logger() {
    // 初始化日志级别字符串
    levelStrings_ = {
        {LogLevel::LevelDebug, "DEBUG"},
        {LogLevel::LevelInfo, "INFO"},
        {LogLevel::LevelWarning, "WARNING"},
        {LogLevel::LevelError, "ERROR"},
        {LogLevel::LevelFatal, "FATAL"}
    };
	logDir_ = std::filesystem::current_path().string() + "\\log";
    openLogFile();
}

// 析构函数
Logger::~Logger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// 限制日志文件的最大数量
void Logger::enforceLogFileLimit() {
	namespace fs = std::filesystem;
	std::vector<std::pair<fs::path, fs::file_time_type>> files;

	// 遍历目录，收集普通文件及其最后修改时间
	for (const auto& entry : fs::directory_iterator(logDir_)) {
		if (fs::is_regular_file(entry.status())) {
			if (entry.path().extension() == ".log") {
				files.emplace_back(entry.path(), fs::last_write_time(entry));
			}
		}
	}
	if (files.size() <= kLogNumLimit) {
		return;
	}
	// 按最后修改时间排序，最早的在前
	std::sort(files.begin(), files.end(),
		[](const auto& a, const auto& b) {
			return a.second < b.second; // 升序，最早的在前
		});
	// 计算需要删除的数量
	size_t numDelete = files.size() - kLogNumLimit;
	for (size_t i = 0; i < numDelete; ++i) {
		const auto& path = files[i].first;
		fs::remove(path);
	}
}

// 打开日志文件
void Logger::openLogFile() {
    namespace fs = std::filesystem;
    if (!fs::exists(logDir_)) {
		if (!fs::create_directories(logDir_)) {
			throw "日志目录" + logDir_ + "创建失败";
            return;
		}
    }
	// 不是目录
	if (!fs::is_directory(logDir_)) {
		throw logDir_ + "文件存在，但不是目录";
		return;
	}
    enforceLogFileLimit();
    logFileName_ = logDir_ + "\\" + getCurrentTimeFileName();
    logFile_.open(logFileName_, std::ios::out | std::ios::app);
    if (!logFile_.is_open()) {
        throw "无法打开日志文件: " + logFileName_;
    }
    return;
}

// 获取带时间戳的文件名
std::string Logger::getCurrentTimeFileName() {
    auto now = std::chrono::system_clock::now();
    __time64_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    std::ostringstream oss;
    oss << "log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";
    return oss.str();
}

// 获取当前时间字符串
std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// 从完整路径中提取文件名
std::string Logger::extractFileName(const std::string& fullPath) {
#ifdef _WIN32
    size_t pos = fullPath.find_last_of("\\/");
#else
    size_t pos = fullPath.find_last_of('/');
#endif
    if (pos != std::string::npos) {
        return fullPath.substr(pos + 1);
    }
    return fullPath;
}

// 内部日志记录函数（带文件名和行号）
void Logger::logInternal(LogLevel level, const std::string& message,
    const std::string& file, int line) {
    if (level < level_) return;
    std::lock_guard<std::mutex> lock(logMutex_);

    // 提取短文件名
    std::string shortFile = extractFileName(file);

    // 构建日志条目
    std::stringstream logStream;

    if (showTimestamp_) {
        // 带时间戳的格式: [时间] [级别] [文件:行号] 消息
        logStream << "[" << getCurrentTime() << "] "
            << "[" << levelStrings_[level] << "] "
            << "[" << shortFile << ":" << line << "] "
            << message;
    }
    else {
        // 不带时间戳的格式: [级别] [文件:行号] 消息
        logStream << "[" << levelStrings_[level] << "] "
            << "[" << shortFile << ":" << line << "] "
            << message;
    }

    std::string logEntry = logStream.str();

    // 写入文件
    if (logFile_.is_open()) {
        logFile_ << logEntry;
        logFile_.flush();  // 即时刷新
        
    }

    // 控制台输出
    if (showConsole_) {
        std::cout << logEntry << std::endl;
    }
}

// LogStream 类实现
LogStream::LogStream(LogLevel level, const std::string& file, int line)
    : level_(level), file_(file), line_(line) {
}

LogStream::~LogStream() {
    std::string message = buffer_.str();
    switch (level_) {
    case LogLevel::LevelDebug:
        Logger::getInstance().debug(message, file_, line_);
        break;
    case LogLevel::LevelInfo:
        Logger::getInstance().info(message, file_, line_);
        break;
    case LogLevel::LevelWarning:
        Logger::getInstance().warning(message, file_, line_);
        break;
    case LogLevel::LevelError:
        Logger::getInstance().error(message, file_, line_);
        break;
    case LogLevel::LevelFatal:
        Logger::getInstance().fatal(message, file_, line_);
        break;
    }
}

// 专门处理 std::endl、std::flush 等操纵符
LogStream& LogStream::operator<<(std::ostream& (*manip)(std::ostream&)) {
    if (manip == static_cast<std::ostream & (*)(std::ostream&)>(std::endl)) {
        // 执行换行 + 刷新日志
        buffer_ << "\n";
        buffer_.flush();  // 或者直接调用你的日志提交逻辑
    }
    else if (manip == static_cast<std::ostream & (*)(std::ostream&)>(std::flush)) {
        buffer_.flush();
    }
    // 你可以选择忽略其他操纵符，或报错
    return *this;
}

LogStreamVoid& LogStreamVoid::operator<<(std::ostream& (*manip)(std::ostream&)) {
    return *this;
}

}// namespace fc_emulator