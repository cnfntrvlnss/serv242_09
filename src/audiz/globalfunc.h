/*************************************************************************
	> File Name: globalfunc.h
	> Author: 
	> Mail: 
	> Created Time: Wed 22 Feb 2017 08:58:19 PM EST
 ************************************************************************/

#ifndef _GLOBALFUNC_H
#define _GLOBALFUNC_H

#include <log4cplus/logger.h>
#include <log4cplus/configurator.h>
#include <log4cplus/loggingmacros.h>

extern log4cplus::Logger g_logger;


#if 0
#define LOG4CPLUS_ERROR(x, ...) std::cerr<< __VA_ARGS__ << endl;
#define LOG4CPLUS_WARN(x, ...) std::cerr<< __VA_ARGS__ << endl;
#define LOG4CPLUS_INFO(x, ...) std::cerr<< __VA_ARGS__ << endl;
#define LOG4CPLUS_DEBUG(x, ...) std::cerr<< __VA_ARGS__ << endl;

#endif
#endif
