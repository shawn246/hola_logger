#ifndef _HOLA_LOGGER_H_
#define _HOLA_LOGGER_H_

#include <string>
#include <vector>
#include <mutex>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sstream>
#include <fstream>
#include <iostream>
#include <deque>
#include <io.h>

#define DEFAULT_MAX_KB          1024 * 1024
#define MAX_LOG_BUFFER          100000

#ifdef WIN32
#define PATH_SEP_CODE          '\\'
#else
#define PATH_SEP_CODE          '/'
#endif

namespace hola {

typedef enum
{
    LOG_FORCE,
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
} LogLevel;

static const std::vector<std::string> s_logAbbr = { "[F]", "[E]", "[W]", "[I]", "[D]", "[T]" };
static const std::vector<std::string> s_logName = { "Force", "Error", "Warning", "Info", "Debug", "Trace" };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////                LogOne                  ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename _OutputStream>
class LogOne
{
public:
    LogOne(_OutputStream &os, LogLevel level = LOG_INFO) : _os(os), _level(level) {
        auto pnow = std::chrono::system_clock::now();
        auto tnow = std::chrono::system_clock::to_time_t(pnow);
        _ss << std::put_time(std::localtime(&tnow), "%Y-%m-%d %H:%M:%S");
        if (_enablems) {
            _ss << "." << std::setfill('0') << std::setw(3) << tnow % 1000;
        }
        _ss << " " << s_logAbbr[level] << " ";
    }

    LogOne(const LogOne<_OutputStream> &one) : _os(one._os), _level(one._level), _ss(one._ss) {
    }

    ~LogOne() {
        if (_level <= _ulevel) {
            std::lock_guard<std::mutex> lock(_mutex);
            _os << _ss.str() + "\n";
        }
    }

    template <typename _Value>
    LogOne & operator<< (const _Value &val) {
        _ss << val;
        return *this;
    }

    static void setLogLevel(LogLevel level) {
        _ulevel = level;
    }

    static void enableMilliSecond(bool enable) {
        _enablems = enable;
    }

private:
    _OutputStream &_os;
    LogLevel _level;
    std::stringstream _ss;

    static LogLevel _ulevel;
    static bool _enablems;
    static std::mutex _mutex;
};

template <typename _OutputStream> LogLevel LogOne<_OutputStream>::_ulevel = LOG_INFO;
template <typename _OutputStream> bool LogOne<_OutputStream>::_enablems = false;
template <typename _OutputStream> std::mutex LogOne<_OutputStream>::_mutex;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////             SimpleLogger            ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SimpleLogger
{
    template <typename _OutputStream> friend class LogOne;
public:
    SimpleLogger(const std::string logFile) : _logFile(logFile) {
        openLog();
    }

    virtual ~SimpleLogger() {
        close();
    }

    bool isOpen() {
        return _ofs.is_open();
    }

    void close() {
        closeLog();
    }

    void setLogLevel(LogLevel level) {
        LogOne<SimpleLogger>::setLogLevel(level);
    }

    void enableMilliSecond(bool enable) {
        LogOne<SimpleLogger>::enableMilliSecond(enable);
    }

protected:
    virtual SimpleLogger & operator<<(const std::string &log) {
        _ofs << log;
        return *this;
    }

    virtual void openLog() {
        if (!_logFile.empty()) {
            _ofs.open(_logFile, std::ios::ate | std::ios::app);
        }
    }

    virtual void closeLog() {
        if (_ofs.is_open()) {
            _ofs.close();
        }
    }

protected:
    std::string _logFile;
    std::ofstream _ofs;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////               HolaLogger             ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class HolaLogger
{
    template <typename _OutputStream> friend class LogOne;
public:
    HolaLogger(const std::string &logFile) :
        _logFile(logFile), _maxSize(DEFAULT_MAX_KB << 10), _curSize(0), _maxNum(10), _exitFlag(false) {
        auto pos = logFile.rfind(PATH_SEP_CODE);
        _logPath = pos == std::string::npos ? "" : logFile.substr(0, pos + 1);
        _logName = pos == std::string::npos ? logFile : logFile.substr(pos + 1);

        openLog();
        listLog();

        _logPtrCur = &_logBufA;
        _logPtrBackup = &_logBufB;
        _logThread = std::thread([this]() { logThread(); });
    }

    virtual ~HolaLogger() {
        close();
    }

    void close() {
        _exitFlag = true;
        _cond.notify_one();
        if (_logThread.joinable()) {
            _logThread.join();
        }
        closeLog();
    }

    void setLogMaxKb(uint32_t maxKb) {
        _maxSize = maxKb << 10;
    }

    void setMaxFileNum(uint32_t maxNum) {
        _maxNum = maxNum < 1 ? 1 : maxNum;
        resizeLog();
    }

    void setLogLevel(LogLevel level) {
        LogOne<HolaLogger>::setLogLevel(level);
    }

    void enableMilliSecond(bool enable) {
        LogOne<HolaLogger>::enableMilliSecond(enable);
    }

private:
    virtual HolaLogger & operator<<(const std::string &log) {
        if (!_exitFlag) {
            std::lock_guard<std::mutex> lock(_mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (_logPtrCur->size() >= MAX_LOG_BUFFER) {
                _cond.notify_one();
            } else {
                _logPtrCur->push_back(log);
            }
        }
        return *this;
    }

    void listLog() {
        long hFile = 0;
        struct _finddata_t fileInfo;
        if ((hFile = _findfirst((_logPath + _logName + "*").c_str(), &fileInfo)) != -1) {
            do {
                if (!(fileInfo.attrib & _A_SUBDIR) && _logName != fileInfo.name) {
                    _logList.emplace_back(_logPath + fileInfo.name);
                }
            } while (_findnext(hFile, &fileInfo) == 0);
            _findclose(hFile);
        }
        resizeLog();
    }

    void resizeLog() {
        while (_maxNum <= _logList.size()) {
            remove(_logList.front().c_str());
            std::cout << "delete " << _logList.front() << std::endl;
            _logList.pop_front();
        }
    }

    virtual void openLog() {
        if (!_logFile.empty()) {
            _ofs.open(_logFile, std::ios::ate | std::ios::app);
        }

        if (_ofs.is_open()) {
            if (_ofs.tellp() == std::streampos(0)) {
                _ofs << std::setfill('#') << std::setw(55) << "" << std::endl;
                _ofs << "#  Abbreviations used in this document" << std::setfill(' ') << std::setw(17) << "#" << std::endl;
                for (size_t i = 0; i < s_logAbbr.size(); i++) {
                    _ofs << "#  " << s_logAbbr[i] << "     " << s_logName[i] << std::setfill(' ')
                        << std::setw(44 - s_logName[i].length()) << "#" << std::endl;
                }
                _ofs << std::setfill('#') << std::setw(55) << "" << std::endl;
            }
            _curSize = _ofs.tellp();
        }
    }

    virtual void closeLog() {
        if (_ofs.is_open()) {
            _ofs.close();
        }
    }

    void switchLog() {
        closeLog();
        auto pnow = std::chrono::system_clock::now();
        auto tnow = std::chrono::system_clock::to_time_t(pnow);
        std::stringstream sstime;
        sstime << std::put_time(std::localtime(&tnow), "%Y-%m-%d-%H-%M-%S");
        std::string nameTo = _logFile + "_" + sstime.str();
        rename(_logFile.c_str(), nameTo.c_str());
        _logList.emplace_back(nameTo);
        resizeLog();
        return openLog();
    }

    void logThread()
    {
        while (!_exitFlag) {
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cond.wait_for(lock, std::chrono::seconds(2));
                if (_logPtrCur->empty()) {
                    continue;
                } else {
                    std::swap(_logPtrCur, _logPtrBackup);
                }
            }

            auto batchLog = [this](std::vector<std::string> *logPtr) {
                for (auto &log : *logPtr) {
                    if (_curSize + log.length() > _maxSize) {
                        switchLog();
                    }
                    _ofs << log;
                    _curSize += log.length();
                }
                _ofs.flush();
                logPtr->clear();
            };

            batchLog(_logPtrBackup);
            if (_exitFlag) {
                batchLog(_logPtrCur);
            }
        }
    }

private:
    std::string _logFile;
    std::deque<std::string> _logList;
    std::string _logPath;
    std::string _logName;
    std::ofstream _ofs;

    std::mutex _mutex;
    std::condition_variable _cond;
    uint32_t _maxSize;
    uint32_t _curSize;
    uint32_t _maxNum;

    bool _exitFlag;
    std::thread _logThread;

    std::vector<std::string> _logBufA;
    std::vector<std::string> _logBufB;
    std::vector<std::string> *_logPtrCur;
    std::vector<std::string> *_logPtrBackup;
};

}
#endif // !_HOLA_LOGGER_H_