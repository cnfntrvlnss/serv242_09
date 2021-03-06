/*************************************************************************
	> File Name: /home/zheng/Desktop/serv242_09/src/audiz/ProjectBuffer.h
	> Author: 
	> Mail: 
	> Created Time: Wed 22 Feb 2017 05:16:31 AM EST
 ************************************************************************/

#ifndef AUDIZ_PROJECTBUFFER_H
#define AUDIZ_PROJECTBUFFER_H
#include <pthread.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <list>

typedef unsigned long uint64_t;//redefination, after cstdint.
//const unsigned UINT_MAX = ;
#include "../audizcomm.h"
#include "../utilities/utilites.h"

extern void reportAudiz_Result(const Audiz_Result& res);
namespace audiz{
/**
 * recv data is parallel with consume data; streams of consuming data are in parallel too; 
 * TODO add comsumer as callback, called in maintain thread. and the cumsumer should surport fixed-audio sample search.
 *
 *
 *
 */

extern char *g_ShmStartPtr;
const unsigned BLOCKSIZE = 48000;
struct ShmBlock{
    int ftokId;
    unsigned offset;
    void set(int ftokId, unsigned offset){
        this->ftokId = ftokId;
        this->offset = offset;
    }
};
struct ShmSegment{
    explicit ShmSegment(const ShmBlock *s):
        blk(s), len(0)
    {}
    const ShmBlock *blk;
    unsigned len;
};


static char * ShmSeg_get(const ShmBlock *seg, unsigned offset)
{
    return g_ShmStartPtr + seg->offset + offset;
}

static void ShmSeg_copy(const ShmBlock* seg, unsigned offset, char *data, unsigned len)
{
    memcpy(ShmSeg_get(seg, offset), data, len);
}


class Project{
public:
    /*
    struct Segment{
        explicit Segment(const ShmBlock *s):
            blk(s), len(0)
        {}
        const ShmBlock *blk;
        unsigned len;
    };*/

    explicit Project(uint64_t id){
        PID = id;
        reset();
    }
    
    void reset(){
        bFull = false;
        bFinished = false;
        ceilUnitIdx = bufferConfig.waitLength / BLOCKSIZE;
        ceilOffset = bufferConfig.waitLength - ceilUnitIdx * BLOCKSIZE;

    }

    void getData(std::vector<ShmSegment> &vec){
        AutoLock l(m_lock);
        vec.clear();
        vec.insert(vec.end(), m_vecAllSegs.begin(), m_vecAllSegs.end());
    }
    bool recvData(uint64_t id, char *data, unsigned len, int &err);
    //return false if it already full.
    bool setFinished()
    {
        AutoLock l(m_lock);
        bFinished = true;
        bool ret = false;
        if(!bFull){
            setFull();
            ret = true;
        }
        return ret;
    }
    bool turnFullByTimeout(struct timeval curtime, struct timeval& nexttime){
        AutoLock l(m_lock);
        if(m_vecAllTimeRecords.size() == 0) return false;
        nexttime.tv_sec = m_vecAllTimeRecords.front().time.tv_sec + bufferConfig.waitSeconds;
        nexttime.tv_usec = 0;
        unsigned tmpsec = m_vecAllTimeRecords.back().time.tv_usec + bufferConfig.waitSecondStep;
        if(tmpsec < nexttime.tv_sec){
            nexttime.tv_sec = tmpsec;
        }
        if(nexttime.tv_sec > curtime.tv_sec){
            setFull();
            return true;
        }
        return false;
    }

    uint64_t PID;

    struct BufferConfig{
        BufferConfig():
            waitLength(UINT32_MAX), waitSeconds(UINT32_MAX), waitSecondStep(60)
        {}
        unsigned waitLength;
        unsigned waitSeconds;
        unsigned waitSecondStep;
    };
    static BufferConfig bufferConfig;
private:
    Project(const Project&);
    Project& operator=(const Project&);
    

    bool recvData(uint64_t id, char* data, unsigned len, std::vector<const ShmBlock*>& seg);
    void setFull(){
            bFull = true;
            if(m_vecAllTimeRecords.size() > 0) fullRecord = m_vecAllTimeRecords.back();
    }

    struct ArrivalRecord{
        explicit ArrivalRecord(struct timeval val = {0, 0}, unsigned idx=0, unsigned jdx=0, unsigned lost = 0):
            time(val), segidx(idx), end(jdx), lostsize(lost)
        {}
        struct timeval time;
        unsigned segidx;
        unsigned end;
        unsigned lostsize;
    };

    LockHelper m_lock;
    //waiting new data timeout; no data to read; having enough data.
    bool bFull;
    ArrivalRecord fullRecord;
    bool bFinished;
    unsigned ceilUnitIdx;
    unsigned ceilOffset;
    std::vector<ShmSegment> m_vecAllSegs;
    std::vector<ArrivalRecord> m_vecAllTimeRecords;
};

class ProjectConsumer{
public:
    virtual bool sendProject(Project *proj) =0;
    void confirm(uint64_t pid, Audiz_Result *res);
};

void addStream(ProjectConsumer *que);
void removeStream(ProjectConsumer *que);

bool initProjPool(const char*);
void rlseProjPool();

bool recvProjSegment(uint64_t id, char *data, unsigned len);
void notifyProjFinish(unsigned long int pid);
unsigned queryProjNum();

};

#endif
