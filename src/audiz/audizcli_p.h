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
#include <string>
#include <vector>

#include "../audizstruct.h"
#define MAX_PATH 512

typedef int (*AUDIZ_REPORTRESULTPROC)(Audiz_Result *pResult);
//typedef int (*AUDIZ_GETALLMDLSPROC)(struct SpkMdlSt **);
struct SpkMdlStVec{
    struct iterator{
        virtual SpkMdlSt* next() =0;
    };
    virtual iterator *iter() =0;
};

/**
 * capsulate session working with servRec.
 * interfaces contain receive data, receive models.
 * pull all models, push results.
 *
 * notes: 
 */
class SessionStruct{
public:
    SessionStruct(const char* servPath, AUDIZ_REPORTRESULTPROC resFunc, SpkMdlStVec *getModlsFunc);
    ~SessionStruct();
    
    bool writeData(Audiz_WaveUnit* unit);
    //TODO add api feedAllSamples
    void feedAllSamples();
    bool feedAllSamples_inner();
    bool writeSample(const char *name, char *buf, unsigned len);
    bool deleteSample(const char *name);
    bool queueSamples(std::vector<std::string> &smps);
    unsigned queryUnfinishedProjNum();
    bool isConnected(){
        pthread_mutex_lock(&isRunLock);
        bool ret = bConnected;
        pthread_mutex_lock(&isRunLock);
        return ret;
    }
    friend void* maintainSession_ex(void* param);
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
    void setIsRun(bool val){
        pthread_mutex_lock(&isRunLock);
        isRunning = val;
        pthread_mutex_unlock(&isRunLock);
    }
    void setConnected(bool val){
        pthread_mutex_lock(&isRunLock);
        bConnected = val;
        pthread_mutex_unlock(&isRunLock);
    }

    void setHasSamples(){
        pthread_mutex_lock(&hasSamplesLock);
        bHasSamples = true;
        pthread_mutex_unlock(&hasSamplesLock);
    }
    bool  getHasSamples(){
        pthread_mutex_lock(&hasSamplesLock);
        bool bret = bHasSamples;
        pthread_mutex_unlock(&hasSamplesLock);
        return bret;
    }

    void closeDataLink(int);
    void closeModlLink(int);
    int checkModlFd(bool btry);
    int checkDataFd(bool btry);
    bool prochandleResp(int fd);
    bool prochandleDataResp(int fd);
    //int procSendCfgCmd();
    int procExecCommonCfgCmd(std::vector<AZ_PckVec>& task, Audiz_PResult &result);

    /////////////////data part//////////////////
    char servPath[MAX_PATH];
    bool isRunning;
    pthread_mutex_t isRunLock;
    bool bHasSamples;
    pthread_mutex_t hasSamplesLock;
    bool bConnected;
    pthread_t modlThreadId;
    AUDIZ_REPORTRESULTPROC repResAddr;
    SpkMdlStVec* retMdlsAddr;
    int dataFd;
    int modlFd;
    pthread_mutex_t modlFdLock;
    pthread_mutex_t dataFdLock;
    //used to commit task to modl link in other than inner thread.
    pthread_mutex_t cfgCmdLock;
    pthread_cond_t cfgCmdResultSetCond;
    pthread_cond_t cfgCmdTaskEmptyCond;
    std::vector<AZ_PckVec> cfgCmdTask;
    Audiz_PResult cfgCmdResult;
};

#endif 
