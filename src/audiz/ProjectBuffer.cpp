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

#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <set>
#include <map>
#include<iostream>

#include "globalfunc.h"

using namespace std;

namespace audiz{

/////////////////////////////shm management///////////////
string g_ShmPath = "recMain";
int g_iFtokId = 0;
int g_iShmId;
char *g_ShmStartPtr = NULL;
set<unsigned> g_AllFreeSegs;//sorted by less than
ShmSegment *g_ShmSegArr;
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
    memcpy(g_ShmStartPtr, "\x01", 1);
    g_ShmSegNum = g_uShmSize / BLOCKSIZE;
    g_ShmSegArr = (ShmSegment *)malloc(sizeof(ShmSegment) * g_ShmSegNum);
    
    for(unsigned idx=0; idx < g_ShmSegNum; idx++){
        g_ShmSegArr[idx].set(0, idx * BLOCKSIZE);
    }
    for(unsigned idx=0; idx < g_ShmSegNum; idx++){
        g_AllFreeSegs.insert(idx);
    }
}

static void rlseShmSegPool()
{
    free(g_ShmSegArr);
    g_ShmSegArr = NULL;
    assert(g_AllFreeSegs.size() == g_ShmSegNum);
    g_AllFreeSegs.clear();
    
    if(shmdt(g_ShmStartPtr) == -1){
        LOG4CPLUS_ERROR(g_logger, "rlseShmSegPool shmdt failed. error: "<< strerror(errno));
    }
    if(shmctl(g_iShmId, IPC_RMID, NULL) == -1){
        LOG4CPLUS_ERROR(g_logger, "rlseShmSegPool shmdt failed. error: "<< strerror(errno));
    }
}

const ShmSegment* ShmSeg_alloc(const ShmSegment *stptr)
{
    const ShmSegment *ret;
    set<unsigned>::iterator it;
    if(stptr == NULL){
         it = g_AllFreeSegs.begin();
        ret = &g_ShmSegArr[*it];
        g_AllFreeSegs.erase(it);
        return ret;
    }
    unsigned st = (stptr - g_ShmSegArr) / sizeof(ShmSegment);
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

void ShmSeg_relse(const ShmSegment *ele)
{
     unsigned st = (ele - g_ShmSegArr) / sizeof(ShmSegment);
    assert(st < g_ShmSegNum);
    g_AllFreeSegs.insert(st);
}


/////////////////////////////project impliment///////////////////
/**
 * consider the conidtion that segs do not contain suffice segment to 
 * hold new arrival data.
 *
 */
bool Project::recvData(uint64_t id, char* data, unsigned len, std::vector<const ShmSegment*>& segs)
{
    LOG4CPLUS_DEBUG(g_logger, "Project::recvData start... id="<< id<< "; len="<< len);
    assert(id == this->PID);
    unsigned rem = len;
    unsigned freeidx = 0;
    while(rem != 0){
        long int leftsize = 0;
        Segment *pseg;
        if(m_vecAllSegs.size() > 0){
            pseg = &m_vecAllSegs[m_vecAllSegs.size() -1];
            leftsize = BLOCKSIZE - pseg->len;
        }
        if(leftsize == 0 || freeidx < segs.size()){
            m_vecAllSegs.push_back(Segment(segs[freeidx]));
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
    const ShmSegment *seg = NULL;
    if(m_vecAllSegs.size() > 0){
        leftsize = BLOCKSIZE - m_vecAllSegs.back().len;
        seg = m_vecAllSegs.back().blk;
    }
    long rem = len - leftsize;
    vector<const ShmSegment*> segs;
    while(rem > 0){
        seg = ShmSeg_alloc(seg);
        if(seg == NULL) break;
        segs.push_back(seg);
    }
    recvData(id, data, len, segs);
    if(m_vecAllSegs.size() > 0){
        unsigned unitidx = m_vecAllSegs.size() - 1;
        unsigned offset = m_vecAllSegs[unitidx].len;
        if(unitidx > ceilUnitIdx || unitidx == ceilUnitIdx && offset >= ceilOffset){
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
    ProjectCheckBook(Project* p, int c):
        prj(p), refcnt(c), bfull(false)
    { 
        checkedtime.tv_sec = 0;
        checkedtime.tv_usec =0;
    }
    Project *prj;
    int refcnt;
    bool bfull;
    struct timeval checkedtime;
};

LockHelper g_ProjPoolLock;
static map<uint64_t, ProjectCheckBook> g_mProjPool;
//static vector<ProjectConsumer*> g_vecConsumers;
static map<ProjectConsumer*, set<uint64_t> > g_mapConsumeProjs;

static list<uint64_t> g_liNewFullProjs;
static pthread_mutex_t g_NewFullLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_NewFullCond = PTHREAD_COND_INITIALIZER;

bool initProjPool()
{
    initShmSegPool();
}

void rlseProjPool()
{
    AutoLock l(g_ProjPoolLock);
    //do preceding work to free project buffers 
    while(g_mProjPool.begin() != g_mProjPool.end()){
        ProjectCheckBook &cur = g_mProjPool.begin()->second;
        //TODO make sure no reference to prj exists.
        while(cur.refcnt != 0){
            
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
        g_mProjPool[pid] = ProjectCheckBook(new Project(pid), 0);
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
    it->second.prj->setFinished();
    g_mProjPool[pid].bfull = true;
    g_ProjPoolLock.unLock();
    //inform monitor thread that Project is full.
    pthread_mutex_lock(&g_NewFullLock);
    g_liNewFullProjs.push_back(pid);
    pthread_mutex_unlock(&g_NewFullLock);
    pthread_cond_broadcast(&g_NewFullCond);
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
void delProjectRefer(uint64_t pid)
{
    int &refcnt = g_mProjPool[pid].refcnt;
     refcnt--;
    assert(refcnt < 0);
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
    AutoLock l(g_ProjPoolLock);
    if(g_mapConsumeProjs.find(this) == g_mapConsumeProjs.end()){
        return;
    } 
    set<uint64_t> &setprjs = g_mapConsumeProjs[this];
    if(setprjs.find(pid) == setprjs.end()){
        return;
    }
    
    //TODO forward result to audizserver_p.

    delProjectRefer(pid);
}

/**
 * TODO try to add full projects to the new stream.
 *
 */
void addStream(ProjectConsumer *que)
{
    AutoLock l(g_ProjPoolLock);
    assert(g_mapConsumeProjs.find(que) == g_mapConsumeProjs.end());
    g_mapConsumeProjs[que];
}

void removeStream(ProjectConsumer *que)
{
    AutoLock l(g_ProjPoolLock);
    assert(g_mapConsumeProjs.find(que) != g_mapConsumeProjs.end());
    set<uint64_t> &pids = g_mapConsumeProjs[que];
    while(pids.size() > 0){
        uint64_t pid = *pids.begin();
        delProjectRefer(pid);
    }
    g_mapConsumeProjs.erase(que);
}

/**
 * monitor data growing of all projects, and do corresponding things on the occurrence of specified events. such as the project is full, new data suffice to do fixed-audio search.
 *
 */
void *poolPoolThread(void *param)
{
    while(true){
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
            //TODO have some full projects.
            fullPrjs = g_liNewFullProjs;
            g_liNewFullProjs.clear();
        }
        pthread_mutex_unlock(&g_NewFullLock);
        while(fullPrjs.size() > 0){
            uint64_t pid = fullPrjs.front();
            g_ProjPoolLock.lock();

            g_ProjPoolLock.unLock();
        }

        g_ProjPoolLock.lock();
        for()
        g_ProjPoolLock.unLock();

        //TODO check all projects for timeout ones.
        for()
    }
}

};//end audiz
