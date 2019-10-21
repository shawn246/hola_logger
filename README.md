# hola_logger
A simple log funciton implementation with one header file
- Thread safe.
- Use only << to logging
- Support for limiting the size of sigle log file

# Usage
```c++
#include "hola_logger.h"

using namespace hola;

int main()
{
    /// for console print
    LogOne<std::ostream>(std::cout) << "hola " << "logger";
    LogOne<std::ostream>(std::cout, LOG_FORCE) << "hola " << "logger";
    
    LogOne<std::ostream>::setLogLevel(LOG_TRACE);
    LogOne<std::ostream>::enableMilliSecond(true);
    LogOne<std::ostream>(std::cout, LOG_TRACE) << "hola " << "logger";
    
    /// for simple log file
    SimpleLogger slogger("hola1.log");
    slogger.enableMilliSecond(true);
    slogger.setLogLevel(LOG_DEBUG);
    LogOne<SimpleLogger>(slogger) << "hola " << "logger";
    LogOne<SimpleLogger>(slogger, LOG_DEBUG) << "hola " << "logger";
    
    /// for hola log file
    HolaLogger logger("hola2.log");
    logger.setLogMaxKb(512);

#define LOGI LogOne<HolaLogger>(logger, LOG_INFO)
    LOGI << "hola " << "logger";
}
```