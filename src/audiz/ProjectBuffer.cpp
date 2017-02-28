/*************************************************************************
> File Name: ProjectBuffer.cpp
> Author: 
> Mail: 
> Created Time: Wed 22 Feb 2017 06:40:49 AM EST
************************************************************************/
#include "ProjectBuffer.h"

#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <set>
#include <map>
#include<iostream>

#include "globalfunc.h"

//from audizserv_p.cpp
extern void startServTask();
extern void endServTask();

using namespace std;

namespace audiz{

/////////////////////////////shm management///////////////
string g_ShmPath = "recMain";
int g_iFtokId = 0;
int g_iShmId;
char *g_ShmStartPtr = NULL;
set<unsigned> g_AllFreeSegs;//sorted by less than
ShmBlock *g_ShmSegArr;
unsigned g_ShmSegNum;
const unsigned uShmCapicity = 2 * 1024 * 1024 * 1024L;
const unsigned g_uShmSize = (uShmCapicity / BLOCKSIZE ) * BLOCKSIZE;

static void initShmSegPool()
{
    key_t key = ftok(g_ShmPath.c_str(), g_iFtokId);
    if(key == -1){
        LOG4CPLUS_ERROR(g_logger, "initShmSegPool ftok failed. path: "<< g_ShmPath.c_str()<< "; error: "<< strerror(errno));
        exit(1);
    }
    g_iFtokId = shmget(key, g_uShmSize, 0600 | IPC_CREAT);
    if(g_iFtokId == -1){
        LOG4CPLUS_ERROR(g_logger, "initShmSegPool shmget failed. error: "<< strerror(errno));
        exit(1);
    }
    g_ShmStartPtr = (char*)shmat(g_iFtokId, 0, SHM_RND);
    LOG4CPLUS_INFO(g_logger, "initShmSegPool have got shared memory. key: "<< key<< "; id: "<< g_iFtokId<< "; Pointer: "<< g_ShmStartPtr);
    memcpy(g_ShmStartPtr, "\x01", 1);
    g_ShmSegNum = g_uShmSize / BLOCKSIZE;
    g_ShmSegArr = (ShmBlock *)malloc(sizeof(ShmBlock) * g_ShmSegNum);

    for(unsigned idx=0; idx < g_ShmSegNum; idx++){
        g_ShmSegArr[idx].set(0, idx * BLOCKSIZE);
    }
    for(unsigned idx=0; idx < g_ShmSegNum; idx++){
        g_AllFreeSegs.insert(idx);
    }
}

static void rlseShmSegPool()
{
    assert(g_AllFreeSegs.size() == g_ShmSegNum);
    g_AllFreeSegs.clear();
    free(g_ShmSegArr);
    g_ShmSegArr = NULL;

    if(shmdt(g_ShmStartPtr) == -1){
        LOG4CPLUS_ERROR(g_logger, "rlseShmSegPool shmdt failed. error: "<< strerror(errno));
    }
    if(shmctl(g_iShmId, IPC_RMID, NULL) == -1){
        LOG4CPLUS_ERROR(g_logger, "rlseShmSegPool shmctl with IPC_RMID failed. error: "<< strerror(errno));
    }
}

const ShmBlock* ShmSeg_alloc(const ShmBlock *stptr)
{
    const ShmBlock *ret;
    set<unsigned>::iterator it;
    if(stptr == NULL){
        it = g_AllFreeSegs.begin();
        ret = &g_ShmSegArr[*it];
        g_AllFreeSegs.erase(it);
        return ret;
    }
    unsigned st = (stptr - g_ShmSegArr) / sizeof(ShmBlock);
    assert( st < g_ShmSegNum );
    it = g_AllFreeSegs.lower_bound(st);
    if(it == g_AllFreeSegs.end()){
        it = g_AllFreeSegs.begin();
    }
    if(it == g_AllFreeSegs.end()){
        LOG4CPLUS_ERROR(g_logger, "shmseg_alloc no free segment.");
        return NULL;
    }
    assert(*it != st);
    ret = &g_ShmSegArr[*it];
    g_AllFreeSegs.erase(it);
    return ret;
}

void ShmSeg_relse(const ShmBlock *ele)
{
    unsigned st = (ele - g_ShmSegArr) / sizeof(ShmBlock);
    assert(st < g_ShmSegNum);
    g_AllFreeSegs.insert(st);
}


/////////////////////////////project impliment///////////////////

Project::BufferConfig Project::bufferConfig;
/**
* consider the conidtion that segs do not contain suffice segment to 
* hold new arrival data.
*
*/
bool Project::recvData(uint64_t id, char* data, unsigned len, std::vector<const ShmBlock*>& segs)
{
    LOG4CPLUS_DEBUG(g_logger, "Project::recvData start... id="<< id<< "; len="<< len);
    assert(id == this->PID);
    unsigned rem = len;
    unsigned freeidx = 0;
    while(rem != 0){
        long int leftsize = 0;
        ShmSegment *pseg;
        if(m_vecAllSegs.size() > 0){
            pseg = &m_vecAllSegs[m_vecAllSegs.size() -1];
            leftsize = BLOCKSIZE - pseg->len;
        }
        if(leftsize == 0 || freeidx < segs.size()){
            m_vecAllSegs.push_back(ShmSegment(segs[freeidx]));
            freeidx ++;
            pseg = &m_vecAllSegs[m_vecAllSegs.size() - 1];
            leftsize = BLOCKSIZE - pseg->len;
        }
        if(leftsize == 0) break;
        if(leftsize > rem){
            leftsize = rem;
        }
        ShmSeg_copy(pseg->blk, pseg->len, data + (len - rem), leftsize);
        pseg->len += leftsize;
        rem -= leftsize;
    }
    assert(freeidx == segs.size());
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
    unsigned segidx = 0;
    unsigned end = 0;
    if(m_vecAllSegs.size() > 0){
        segidx = m_vecAllSegs.size() - 1;
        end = m_vecAllSegs[segidx].len;
    }
    m_vecAllTimeRecords.push_back(ArrivalRecord(curtime, segidx, end, rem));
    LOG4CPLUS_DEBUG(g_logger, "Project::recvData finished. id="<< id<< "; len="<< len<< "; LostSize="<< rem);
    return true;
}

/**
*
* return err = 1 when it turns full.
*/
bool Project::recvData(uint64_t id, char* data, unsigned len, int &err)
{
    err = 0;
    AutoLock l(this->m_lock);
    unsigned leftsize = 0;
    const ShmBlock *seg = NULL;
    if(m_vecAllSegs.size() > 0){
        leftsize = BLOCKSIZE - m_vecAllSegs.back().len;
        seg = m_vecAllSegs.back().blk;
    }
    long rem = len - leftsize;
    vector<const ShmBlock*> segs;
    while(rem > 0){
        seg = ShmSeg_alloc(seg);
        if(seg == NULL) break;
        segs.push_back(seg);
        rem -= BLOCKSIZE;
    }
    recvData(id, data, len, segs);
    if(m_vecAllSegs.size() > 0){
        unsigned unitidx = m_vecAllSegs.size() - 1;
        unsigned offset = m_vecAllSegs[unitidx].len;
        if(unitidx > ceilUnitIdx || (unitidx == ceilUnitIdx && offset >= ceilOffset)){
            if(!bFull){
                setFull();
                err = 1;
                LOG4CPLUS_DEBUG(g_logger, "Project::recvData PID="<< PID<< " turn full as the length of data reach to threshold.");
            }
        }
    }

    return 0;
}

//////////////////////////////////projects' pool/////////////////////

struct ProjectCheckBook{
    explicit ProjectCheckBook(Project* p=NULL, int c=0):
    prj(p), refcnt(c), bfull(false)
    { 
        expiredtime.tv_sec = 0;
        expiredtime.tv_nsec =0;
    }
    Project *prj;
    int refcnt;
    bool bfull;
    struct timespec expiredtime;
};

LockHelper g_ProjPoolLock;
static map<uint64_t, ProjectCheckBook> g_mProjPool;
static map<ProjectConsumer*, set<uint64_t> > g_mConsumeProjs;
static LockHelper g_RmAddConsumeMapLock;

static pthread_t g_PoolThrdId;
static bool g_bPoolThrdRun = true;
static pthread_mutex_t g_PoolRunLock = PTHREAD_MUTEX_INITIALIZER;
//projs full, but not refered.
static list<uint64_t> g_liNewFullProjs;
static pthread_mutex_t g_NewFullLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_NewFullCond = PTHREAD_COND_INITIALIZER;

static void *poolPollThread(void *param);

static inline void setPoolThrdRun()
{
    pthread_mutex_lock(&g_PoolRunLock);
    g_bPoolThrdRun = false;
    pthread_mutex_unlock(&g_PoolRunLock);
}
static inline bool getPoolThrdRun()
{
    pthread_mutex_lock(&g_PoolRunLock);
    bool ret = g_bPoolThrdRun;
    pthread_mutex_unlock(&g_PoolRunLock);
    return ret;
}

bool initProjPool()
{
    initShmSegPool();
    int retp = pthread_create(&g_PoolThrdId, NULL, poolPollThread, NULL);
    if(retp != 0){
        LOG4CPLUS_ERROR(g_logger, "initProjPool failed to create thread poolPollThread.");
        exit(1);
    }
    //after projpool init.
    startServTask();
    return true;
}

void rlseProjPool()
{
    //befor projpool rlse.
    endServTask();
    //TODO make sure no project turn false.

    setPoolThrdRun();
    pthread_join(g_PoolThrdId, NULL);

    AutoLock l(g_ProjPoolLock);
    //TODO make sure all send projectes are confirmed.
    //by making value part of g_mConsumeProjs is empty, or refcnt in g_mProjPool is zero.
    while(g_mProjPool.begin() != g_mProjPool.end()){
        ProjectCheckBook &cur = g_mProjPool.begin()->second;
        while(cur.refcnt != 0){
            sleep(1);   
        }
        delete cur.prj;
        g_mProjPool.erase(g_mProjPool.begin());
    }

    rlseShmSegPool();
}


bool recvProjSegment(uint64_t pid, char *data, unsigned len)
{
    g_ProjPoolLock.lock();
    if(g_mProjPool.find(pid) == g_mProjPool.end()){
        g_mProjPool[pid] = ProjectCheckBook(new Project(pid));
    }   
    ProjectCheckBook& cur = g_mProjPool[pid];
    g_ProjPoolLock.unLock();
    int err;
    cur.prj->recvData(pid, data, len, err);
    if(err == 1){
        //inform monitor thread that Project is full.
        g_ProjPoolLock.lock();
        g_mProjPool[pid].bfull = true;
        g_ProjPoolLock.unLock();
        pthread_mutex_lock(&g_NewFullLock);
        g_liNewFullProjs.push_back(pid);
        pthread_mutex_unlock(&g_NewFullLock);
        pthread_cond_broadcast(&g_NewFullCond);
    }
    return true;
}

void notifyProjFinish(unsigned long int pid)
{
    g_ProjPoolLock.lock();
    map<uint64_t, ProjectCheckBook>::iterator it = g_mProjPool.find(pid);
    if(it == g_mProjPool.end()){
        g_ProjPoolLock.unLock();
        return;
    }

    if(it->second.prj->setFinished()){
        LOG4CPLUS_DEBUG(g_logger, "notifyProjFinish PID="<< pid<< " turn full as reaching eof.");
        g_mProjPool[pid].bfull = true;
        g_ProjPoolLock.unLock();
        //inform monitor thread that Project is full.
        pthread_mutex_lock(&g_NewFullLock);
        g_liNewFullProjs.push_back(pid);
        pthread_mutex_unlock(&g_NewFullLock);
        pthread_cond_broadcast(&g_NewFullCond);
    }
}

unsigned queryProjNum()
{
    AutoLock l(g_ProjPoolLock);
    return g_mProjPool.size();
}

/**
* decrease refcnt of project in pool. and release project if refcnt == 0.
* after PoolPollThread process the proj.
*/
static inline void delProjectRefer(uint64_t pid)
{
    int &refcnt = g_mProjPool[pid].refcnt;
    assert(refcnt > 0);
    refcnt--;
    if(refcnt == 0){
        delete g_mProjPool[pid].prj;
        g_mProjPool.erase(pid);
    }
}

/**
* if the stream is not registered, or project isnot waiting for result,
* just ignore the result.
*
*/
void ProjectConsumer::confirm(uint64_t pid, Audiz_Result *res)
{
    if(res != NULL){
        //forward result to audizserver_p.
        reportAudiz_Result(*res);
    }

    AutoLock l(g_ProjPoolLock);
    if(g_mConsumeProjs.find(this) == g_mConsumeProjs.end()){
        return;
    } 
    set<uint64_t> &setprjs = g_mConsumeProjs[this];
    if(setprjs.find(pid) == setprjs.end()){
        return;
    }
    setprjs.erase(pid);
    delProjectRefer(pid);
}

/**
* TODO add full projects to the new stream.
*
*/
void addStream(ProjectConsumer *que)
{
    AutoLock ol(g_RmAddConsumeMapLock);
    AutoLock l(g_ProjPoolLock);
    assert(g_mConsumeProjs.find(que) == g_mConsumeProjs.end());
    g_mConsumeProjs[que];
}

void removeStream(ProjectConsumer *que)
{
    AutoLock ol(g_RmAddConsumeMapLock);
    AutoLock l(g_ProjPoolLock);
    assert(g_mConsumeProjs.find(que) != g_mConsumeProjs.end());
    set<uint64_t> &pids = g_mConsumeProjs[que];
    while(pids.size() > 0){
        uint64_t pid = *pids.begin();
        delProjectRefer(pid);
    }
    g_mConsumeProjs.erase(que);
}

void sendProject2AllConsumers(unsigned pid)
{

}

/**
* monitor data growing of all projects, and do corresponding things on the occurrence of specified events. such as the project is full, new data suffice to do fixed-audio search.
*
*/
void *poolPollThread(void *param)
{
    while(getPoolThrdRun()){
        list<uint64_t> fullPrjs;
        int waitsecs = 6;
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        struct timespec abstime;
        abstime.tv_sec = curtime.tv_sec + waitsecs;
        abstime.tv_nsec = curtime.tv_usec * 1000;
        pthread_mutex_lock(&g_NewFullLock);
        if(g_liNewFullProjs.size() == 0){
            pthread_cond_timedwait(&g_NewFullCond, &g_NewFullLock, &abstime);
        }
        if(g_liNewFullProjs.size() > 0){
            fullPrjs = g_liNewFullProjs;
            g_liNewFullProjs.clear();
        }
        //check all projects for timeout ones.
        for(map<uint64_t, ProjectCheckBook>::iterator it=g_mProjPool.begin(); it!=g_mProjPool.end(); it++){
            if(it->second.bfull) continue;
            struct timespec &expired = it->second.expiredtime;
            if(expired.tv_sec > abstime.tv_sec) continue;
            struct timeval nexttime;
            struct timeval curtime;
            curtime.tv_sec = abstime.tv_sec;
            curtime.tv_usec = abstime.tv_nsec /1000;
            if(it->second.prj->turnFullByTimeout(curtime, nexttime)){
                LOG4CPLUS_DEBUG(g_logger, "poolPollThread PID="<< it->first<< " turn full for timeout.");
                it->second.bfull = true;
                fullPrjs.push_back(it->first);
            }
            else{
                expired.tv_sec = nexttime.tv_sec;
                expired.tv_nsec = nexttime.tv_usec * 1000;
            }
        }
        pthread_mutex_unlock(&g_NewFullLock);

        list<Project*> curprjs;

        //consume full projects.
        while(fullPrjs.size() > 0){
            list<ProjectConsumer*> appendings;

            g_ProjPoolLock.lock();
            uint64_t pid = fullPrjs.front();
            Project* prj = g_mProjPool[pid].prj;
            curprjs.push_back(prj);
            g_mProjPool[pid].refcnt ++;

            for(map<ProjectConsumer*, set<uint64_t> >::iterator it = g_mConsumeProjs.begin(); it != g_mConsumeProjs.end(); it++){
                it->second.insert(pid);
                g_mProjPool[pid].refcnt ++;
                appendings.push_back(it->first);
            }
            g_ProjPoolLock.unLock();

            while(appendings.size() > 0){
                //make sure the cumsomer is not removed.
                AutoLock ol(g_RmAddConsumeMapLock);
                if(!appendings.front()->sendProject(prj)){
                    g_ProjPoolLock.lock();
                    delProjectRefer(pid);
                    g_ProjPoolLock.unLock();
                }
                appendings.pop_front();
            }
            fullPrjs.pop_front();
        }

        while(curprjs.size() > 0){
            delProjectRefer(curprjs.front()->PID);
            curprjs.pop_front();
        }
    }
        return NULL;
}

};//end audiz
