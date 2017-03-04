/*************************************************************************
	> File Name: samplelib.cpp
	> Author: 
	> Mail: 
	> Created Time: Wed 01 Mar 2017 08:40:38 PM EST
 ************************************************************************/
#include "samplelib.h"

#include <unistd.h>
#include <pthread.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <set>
#include<iostream>

#include "globalfunc.h"
#include "../utilities/utilites.h"
using namespace std;

namespace audiz{

#ifdef _AUDIZ_GLOBALFUNC_H
#define MYLOGE(x) LOG4CPLUS_ERROR(g_logger, x);
#define MYLOGI(x) LOG4CPLUS_INFO(g_logger, x);
#define MYLOGD(x) LOG4CPLUS_DEBUG(g_logger, x);
#endif
#define OUTPUT_ERRNO " error: "<< strerror(errno)
static pthread_mutex_t g_SmpCmsLock = PTHREAD_MUTEX_INITIALIZER;
static set<SampleConsumer*> g_AllSmpConsumers;
static unsigned g_SmpNum;
static bool g_bStored =false;

const char* g_SampleDir = "samples/";

void addSmpConsumer(SampleConsumer* cmr)
{
    pthread_mutex_lock(&g_SmpCmsLock);
    g_AllSmpConsumers.insert(cmr);
    if(g_bStored){
        cmr->feedAll();
    }
    pthread_mutex_unlock(&g_SmpCmsLock);
}

void rmSmpConsumer(SampleConsumer* cmr)
{
    pthread_mutex_lock(&g_SmpCmsLock);
    g_AllSmpConsumers.erase(cmr);
    pthread_mutex_unlock(&g_SmpCmsLock);
}

static bool removeFile(const char* dir, const char *file)
{
    string filepath = concatePath(dir, file);
    if(unlink(filepath.c_str()) == -1){
        MYLOGE("removeFile failed to unlink file "<< filepath.c_str()<< " "<< OUTPUT_ERRNO);
        return false;
    }
    return true;
}
/**
 * remove all samples.
 *
 */
void initSampleLib()
{
    unsigned num = procFilesInDir(g_SampleDir, removeFile);
    MYLOGI("initSampleLib have removed "<< num <<" samples.");
    pthread_mutex_lock(&g_SmpCmsLock);
    g_SmpNum = 0;
    g_bStored = false;
    pthread_mutex_unlock(&g_SmpCmsLock);
}

bool storeSample(const char *head, char *data, unsigned len)
{
    string filepath = concatePath(g_SampleDir, head);
    if(access(filepath.c_str(), F_OK) == 0){
        if(unlink(filepath.c_str()) != 0){
            MYLOGE("storeSample failed to store sample, as failed to remove old file "<< filepath.c_str()<< OUTPUT_ERRNO);
            return false;
        }
    }
    FILE *fp = fopen(filepath.c_str(), "wb");
    if(fp == NULL){
        MYLOGE("storeSample failed to store sample, as failed to open file "<< filepath<< OUTPUT_ERRNO);
        return false;
    }
    fwrite(data, 1, len, fp);
    fclose(fp);
    pthread_mutex_lock(&g_SmpCmsLock);
    g_SmpNum ++;
    pthread_mutex_unlock(&g_SmpCmsLock);
    return true;
}

/**
 *
 * only be triggered at establishing new mdl link of servtask so far,
 * TODO should extend to be triggered by at establishing rec link of servrec. 
 */
void finishStore()
{
    pthread_mutex_lock(&g_SmpCmsLock);
    if(!g_bStored){
        for(set<SampleConsumer*>::iterator it=g_AllSmpConsumers.begin(); it != g_AllSmpConsumers.end(); it++){
            (*it)->feedAll();
        }
        g_bStored = true;
    }
    pthread_mutex_unlock(&g_SmpCmsLock);
}

unsigned getSampleNum()
{
    pthread_mutex_lock(&g_SmpCmsLock);
    unsigned ret = g_SmpNum;
    pthread_mutex_unlock(&g_SmpCmsLock);
    return g_SmpNum;
}

void addSample(const char *head, char* data, unsigned len)
{
    if(!storeSample(head, data, len)){
        return;
    }
    
    pthread_mutex_lock(&g_SmpCmsLock);
    for(set<SampleConsumer*>::iterator it=g_AllSmpConsumers.begin(); it != g_AllSmpConsumers.end(); it++){
        (*it)->addOne(head);
    }
    pthread_mutex_unlock(&g_SmpCmsLock);
}

void rmSample(const char *head)
{
    string filepath = concatePath(g_SampleDir, head);
    if(access(filepath.c_str(), F_OK) == 0){
        if(unlink(filepath.c_str()) != 0){
            MYLOGE("rmSample failed to remove sample file."<< filepath.c_str()<< OUTPUT_ERRNO);
            return ;
        }
    }
    
    pthread_mutex_lock(&g_SmpCmsLock);
    for(set<SampleConsumer*>::iterator it=g_AllSmpConsumers.begin(); it != g_AllSmpConsumers.end(); it++){
        (*it)->rmOne(head);
    }
    pthread_mutex_unlock(&g_SmpCmsLock);
}

}
