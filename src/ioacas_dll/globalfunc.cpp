/*************************************************************************
	> File Name: globalfunc.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 09 Feb 2017 03:24:39 AM EST
 ************************************************************************/

#include "globalfunc.h"

#include <pthread.h>

#include <cassert>
#include <cstdarg>
#include<iostream>
using namespace std;


zsummer::log4z::ILog4zManager* initLog4z()
{
    static const char*myLogCfg="\
        [main]\n\
        path=./ioacas/log/\n\
        level=INFO\n\
        display=true\n\
        monthdir=false\n\
        fileline=false\n\
        enable=true\n\
        outfile=false\n\
        [ioacas]\n\
        path=./ioacas/log/\n\
        level=INFO\n\
        display=true\n\
        monthdir=false\n\
        fileline=false\n\
        enable=true\n\
        outfile=false\n\
        ";
    zsummer::log4z::ILog4zManager* tmpPtr = zsummer::log4z::ILog4zManager::getPtr();
    tmpPtr->configFromString(myLogCfg);   
    tmpPtr->config("ioacas/log4z.ini");
    tmpPtr->setAutoUpdate(6);
    tmpPtr->start();
    return tmpPtr;
}

zsummer::log4z::ILog4zManager *g_Log4zManager = initLog4z();
LoggerId g_logger = LOG4Z_MAIN_LOGGER_ID;


//////////////////////////////////////////////////////////////////////////
//! LockHelper
//////////////////////////////////////////////////////////////////////////
class LockHelper
{
public:
    LockHelper();
    virtual ~LockHelper();

public:
    void lock();
    void unLock();
//private:
#ifdef WIN32
    CRITICAL_SECTION _crit;
#else
    pthread_mutex_t  _crit;
#endif
};

LockHelper::LockHelper()
{
#ifdef WIN32
    InitializeCriticalSection(&_crit);
#else
    //_crit = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
    //pthread_mutexattr_t attr;
    //pthread_mutexattr_init(&attr);
    //pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    //pthread_mutex_init(&_crit, &attr);
    //pthread_mutexattr_destroy(&attr);
    pthread_mutex_init(&_crit, NULL);
#endif
}
LockHelper::~LockHelper()
{
#ifdef WIN32
    DeleteCriticalSection(&_crit);
#else
    pthread_mutex_destroy(&_crit);
#endif
}

void LockHelper::lock()
{
#ifdef WIN32
    EnterCriticalSection(&_crit);
#else
    pthread_mutex_lock(&_crit);
#endif
}
void LockHelper::unLock()
{
#ifdef WIN32
    LeaveCriticalSection(&_crit);
#else
    pthread_mutex_unlock(&_crit);
#endif
}

//////////////////////////////////////////////////////////////////////////
//! AutoLock
//////////////////////////////////////////////////////////////////////////
class AutoLock
{
public:
    explicit AutoLock(LockHelper & lk):_lock(lk){_lock.lock();}
    ~AutoLock(){_lock.unLock();}
private:
    LockHelper & _lock;
};

static std::map<pthread_t, std::vector<std::pair<struct timespec, std::string> > > allClockStack;
static LockHelper clockoutput_lock;
void clockoutput_start(const char *fmt, ...)
{
    clockoutput_lock.lock();
    std::vector<std::pair<struct timespec, std::string> > & cur = allClockStack[pthread_self()];
    struct timespec tmptm;
    clock_gettime(CLOCK_REALTIME, &tmptm);
    cur.push_back(std::make_pair(tmptm, ""));
    va_list args;
    va_start(args, fmt);
    cur[cur.size() -1].second.resize(100);
    int retp = vsnprintf(const_cast<char*>(cur[cur.size() -1].second.c_str()), 100, fmt, args);
    cur[cur.size() - 1].second.resize(retp);
    clockoutput_lock.unLock();
}

std::string clockoutput_end()
{
    AutoLock myLock(clockoutput_lock);
    pthread_t thd = pthread_self();
    assert(allClockStack.find(thd) != allClockStack.end());
    std::vector<std::pair<struct timespec, std::string> > & cur = allClockStack[thd];
    struct timespec lastclock = cur[cur.size() -1].first;
    ostringstream oss;
    struct timespec tmptm;
    clock_gettime(CLOCK_REALTIME, &tmptm);
    unsigned long elsus = (tmptm.tv_sec - lastclock.tv_sec) * 1000000 + (tmptm.tv_nsec - lastclock.tv_nsec) / 1000;
    oss<< cur[cur.size() - 1].second << " ElapseClock "<< elsus;
    cur.pop_back();
    return oss.str();
}

