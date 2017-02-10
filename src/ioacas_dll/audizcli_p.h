/*************************************************************************
    > File Name: AUDIZCLI_P__H
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Wed 24 Aug 2016 07:10:23 PM PDT
 ************************************************************************/
#ifndef AUDIZCLI_P__H
#define AUDIZCLI_P__H

#include <pthread.h>
#include <climits>
#include<iostream>
#include<queue>
using namespace std;

#include "../audizstruct.h"
#define MAX_PATH 512

typedef int (*AUDIZ_REPORTRESULTPROC)(Audiz_Result *pResult);
typedef int (*AUDIZ_GETALLMDLSPROC)(struct SpkMdlSt **);

/**
 * capsulate session working with servRec.
 * interfaces contain receive data, receive models.
 * pull all models, push results.
 *
 * notes: 
 */
class SessionStruct{
public:
    SessionStruct(const char* servPath, AUDIZ_REPORTRESULTPROC resFunc, AUDIZ_GETALLMDLSPROC getModlsFunc);
    ~SessionStruct();

    bool writeData(unsigned long long id, char *buf, unsigned len);
    bool writeModl(const char *name, char *buf, unsigned len);

    friend void* maintainSession(void* param);
    ////////////////data part//////////////////
private:
    SessionStruct(const SessionStruct&);
    SessionStruct& operator=(const SessionStruct&);
    bool connectServRec();
    void closeServRec();
    bool getIsRun(){
        bool retb;
        pthread_mutex_lock(&isRunLock);
        retb = isRunning;
        pthread_mutex_unlock(&isRunLock);
        return retb;
    }
    void closeDataLink();
    void closeModlLink();
    void setIsRun(bool val){
        pthread_mutex_lock(&isRunLock);
        isRunning = val;
        pthread_mutex_unlock(&isRunLock);
    }

    /////////////////data part//////////////////
    char servPath[MAX_PATH];
    bool isRunning;
    pthread_mutex_t isRunLock;
    pthread_t tid4RepRes;
    AUDIZ_REPORTRESULTPROC repResAddr;
    AUDIZ_GETALLMDLSPROC retMdlsAddr;
    int dataFd;
    int modlFd;
    int ressFd;
    pthread_mutex_t fdsLock;
};

#endif 
