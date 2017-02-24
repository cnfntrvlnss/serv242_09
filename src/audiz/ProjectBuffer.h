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
#include "../audizcomm.h"
#include "../utilites.h"

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
struct ShmSegment{
    int ftokId;
    unsigned offset;
    //unsigned len;
    void set(int ftokId, unsigned offset){
        this->ftokId = ftokId;
        this->offset = offset;
    }
};

char * ShmSeg_get(const ShmSegment *seg, unsigned offset)
{
    return g_ShmStartPtr + seg->offset + offset;
}

void ShmSeg_copy(const ShmSegment* seg, unsigned offset, char *data, unsigned len)
{
    memcpy(ShmSeg_get(seg, offset), data, len);
}

class Project{
public:
    struct Segment{
        explicit Segment(const ShmSegment *s):
            blk(s), len(0)
        {}
        const ShmSegment *blk;
        unsigned len;
    };

    explicit Project(uint64_t id){
        PID = id;
        reset();
    }
    
    void reset(){
        bFull = false;
        bFinished = false;
        ceilUnitIdx = bufferConfig.waitLengh / BLOCKSIZE;
        ceilOffset = bufferConfig.waitLength - ceilUnitIdx * BLOCKSIZE;



    }

    void getData(std::vector<const ShmSegment*> &vec);   
    bool recvData(uint64_t id, char* data, unsigned len, std::vector<const ShmSegment*>& seg);
    bool recvData(uint64_t id, char *data, unsigned len, int &err);
    void setFinished()
    {
        bFinished = true;
        AutoLock l(m_lock);
        if(!bFull) setFull();
    }

    uint64_t PID;

    struct BufferConfig{
        BufferConfig():
            waitLength(60), waitSeconds(UINT_MAX), waitSecondStep(UINT_MAX)
        {}
        unsigned waitLength;
        unsigned waitSeconds;
        unsigned waitSecondStep;
    };
    static BufferConfig bufferConfig;
private:
    Project(const Project&);
    Project& operator=(const Project&);

    void setFull(){
            bFull = true;
            if(m_vecAllTimeRecords.size() > 0) fullRecord = m_vecAllTimeRecords.back();
    }

    //const struct timeval ZEROTIME = {0, 0};
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
    std::vector<Segment> m_vecAllSegs;
    std::vector<ArrivalRecord> m_vecAllTimeRecords;
};

class ProjectConsumer{
public:
    virtual void sendProject(Project *proj) =0;
    void confirm(uint64_t pid, Audiz_Result *res);
};

void addStream(ProjectConsumer *que);
void removeStream(ProjectConsumer *que);

bool initProjPool();
void rlseProjPool();

bool recvProjSegment(uint64_t id, char *data, unsigned len);
void notifyProjFinish(unsigned long int pid);
unsigned queryProjNum();
};

#endif
