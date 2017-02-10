/*************************************************************************
	> File Name: globalfunc.h
	> Author: 
	> Mail: 
	> Created Time: Thu 09 Feb 2017 03:23:14 AM EST
 ************************************************************************/

#ifndef _GLOBALFUNC_H
#define _GLOBALFUNC_H

#include "log4z.h"

extern zsummer::log4z::ILog4zManager *g_Log4zManager;
extern LoggerId g_logger;

#define CHECK_PERFOMANCE
#ifdef CHECK_PERFOMANCE

void clockoutput_start(const char *fmt, ...);
std::string clockoutput_end();
#else
static void clockoutput_start(const char *fmt, ...){}
static std::string clockoutput_end(){return ""}
#endif 

#endif
