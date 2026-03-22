#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <fstream>



namespace fc_emulator {
//#undef _DEBUG

// 日志文件的最大数量
constexpr size_t kLogNumLimit = 5;
#ifdef _DEBUG
#define LOG_SET_LEVEL(level) fc_emulator::Logger::getInstance().setLogLevel((level))
#define LOG_GET_LEVEL(level) fc_emulator::Logger::getInstance().getLogLevel((level))
// ================ printf风格宏定义 ================
// 这些宏使用起来就像printf一样，会自动添加文件名和行号
#define LOG_DEBUG(format, ...) fc_emulator::Logger::getInstance().debug(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) fc_emulator::Logger::getInstance().info(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) fc_emulator::Logger::getInstance().warning(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) fc_emulator::Logger::getInstance().error(__FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) fc_emulator::Logger::getInstance().fatal(__FILE__, __LINE__, format, ##__VA_ARGS__)

// ================ 流式日志宏 ================
// 保留流式日志宏，类似cout的使用方式
#define LOG_STREAM_DEBUG fc_emulator::LogStream(fc_emulator::LogLevel::LevelDebug, __FILE__, __LINE__)
#define LOG_STREAM_INFO fc_emulator::LogStream(fc_emulator::LogLevel::LevelInfo, __FILE__, __LINE__)
#define LOG_STREAM_WARNING fc_emulator::LogStream(fc_emulator::LogLevel::LevelWarning, __FILE__, __LINE__)
#define LOG_STREAM_ERROR fc_emulator::LogStream(fc_emulator::LogLevel::LevelError, __FILE__, __LINE__)
#define LOG_STREAM_FATAL fc_emulator::LogStream(fc_emulator::LogLevel::LevelFatal, __FILE__, __LINE__)
#else
// 用于编译器优化，通常编译器会忽略 if (false) { ... }
// 从而把整个 if 翻译为空，不消耗任何性能
constexpr bool bDebug = false;
#define LOG_SET_LEVEL(level)  
#define LOG_GET_LEVEL(level)  
// ================ printf风格宏定义 ================
// 这些宏使用起来就像printf一样，会自动添加文件名和行号
#define LOG_DEBUG(format, ...) 
#define LOG_INFO(format, ...) 
#define LOG_WARNING(format, ...) 
#define LOG_ERROR(format, ...) 
#define LOG_FATAL(format, ...)  

// ================ 流式日志宏 ================
// 保留流式日志宏，类似cout的使用方式
#define LOG_STREAM_DEBUG        if (bDebug) fc_emulator::LogStreamVoid()
#define LOG_STREAM_INFO         if (bDebug) fc_emulator::LogStreamVoid()
#define LOG_STREAM_WARNING      if (bDebug) fc_emulator::LogStreamVoid()
#define LOG_STREAM_ERROR        if (bDebug) fc_emulator::LogStreamVoid()
#define LOG_STREAM_FATAL        if (bDebug) fc_emulator::LogStreamVoid()
#endif //DEBUG
// 日志级别枚举
enum class LogLevel {
    LevelDebug,
    LevelInfo,
    LevelWarning,
    LevelError,
    LevelFatal
};

class Logger {
public:
    // 获取单例实例

	// 静态成员初始化
	inline static Logger& getInstance() {
		static Logger instance;
		return instance;
	}

    // 禁止拷贝和赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setLogLevel(LogLevel level) { level_ = level;  }
    const LogLevel getLogLevel() const { return level_; }

    // 设置是否在控制台显示
    void setShowConsole(bool show);

    // 设置是否显示时间戳
    void setShowTimestamp(bool show);

    // 不同级别的日志方法（带文件名和行号）
    void debug(const std::string& message, const std::string& file, int line);
    void info(const std::string& message, const std::string& file, int line);
    void warning(const std::string& message, const std::string& file, int line);
    void error(const std::string& message, const std::string& file, int line);
    void fatal(const std::string& message, const std::string& file, int line);

    // printf风格日志方法（带文件名和行号）
    template<typename... Args>
    void debug(const std::string& file, int line, const char* format, Args... args);

    template<typename... Args>
    void info(const std::string& file, int line, const char* format, Args... args);

    template<typename... Args>
    void warning(const std::string& file, int line, const char* format, Args... args);

    template<typename... Args>
    void error(const std::string& file, int line, const char* format, Args... args);

    template<typename... Args>
    void fatal(const std::string& file, int line, const char* format, Args... args);

    // 获取当前日志文件名
    std::string getLogFileName() const;


    void enforceLogFileLimit();
    // 重新打开日志文件（用于日志轮转）
    void reopenLogFile();

private:
    Logger();
    ~Logger();

    void openLogFile();
    std::string getCurrentTimeFileName();
    std::string getCurrentTime();
    std::string extractFileName(const std::string& fullPath);
    void logInternal(LogLevel level, const std::string& message,
        const std::string& file, int line);

    std::ofstream logFile_;
    std::mutex logMutex_;
    std::string logDir_;
    std::string logFileName_;
    bool showConsole_ = false;      // 是否在控制台显示
    bool showTimestamp_ = false;    // 是否显示时间戳

    // 日志级别
    LogLevel level_;
    std::map<LogLevel, std::string> levelStrings_;
};

// 流式日志辅助类
class LogStream {
public:
    LogStream(LogLevel level, const std::string& file, int line);
    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T& value);
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&));
private:
    LogLevel level_;
    std::string file_;
    int line_;
    std::stringstream buffer_;
};

// ================ 模板方法实现 ================
// printf风格的模板函数实现
template<typename... Args>
void Logger::debug(const std::string& file, int line, const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    debug(buffer, file, line);
}

template<typename... Args>
void Logger::info(const std::string& file, int line, const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    info(buffer, file, line);
}

template<typename... Args>
void Logger::warning(const std::string& file, int line, const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    warning(buffer, file, line);
}

template<typename... Args>
void Logger::error(const std::string& file, int line, const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    error(buffer, file, line);
}

template<typename... Args>
void Logger::fatal(const std::string& file, int line, const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    fatal(buffer, file, line);
}

// 流式日志模板函数实现
template<typename T>
LogStream& LogStream::operator<<(const T& value) {
    buffer_ << value;
    return *this;
}

// 流式日志输出到黑洞，适配#ifndef DEBUG时的流式输出语句
class LogStreamVoid {
public:
    template<typename T>
    LogStreamVoid& operator<<(const T&) {
        return *this;
    }

    LogStreamVoid& operator<<(std::ostream& (*manip)(std::ostream&));
};


}// namespace fc_emulator