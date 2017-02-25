/*************************************************************************
	> File Name: recMain.cpp
	> Author: 
	> Mail: 
	> Created Time: Wed 22 Feb 2017 11:09:35 PM EST
 ************************************************************************/

#include <unistd.h>

#include <cstdint>
#include<iostream>
#include <string>

#include "ProjectBuffer.h"

using namespace audiz;
using namespace std;

string g_strSaveDir = "tmpData/";
class ProjectConsumerImpl: public ProjectConsumer{
    bool SendProject(Project* prj);
};

bool ProjectConsumerImpl::SendProject(Project* prj)
{
   char filepath[512];
    sprintf(filepath, "%s%lu.wav", g_strSaveDir.c_str(), prj->PID);
    FILE *fp = fopen(filepath, "wb");
    if(fp == NULL){
        fprintf(stderr, "failed to open file %s.\n", filepath);
        return false;
    }
    vector<Project::Segment> segs;
    prj->getData(segs);
    for(size_t idx=0; idx < segs.size(); idx++){
        const ShmSegment *seg = segs[idx].blk;
        unsigned len = segs[idx].len;
        char *stptr = ShmSeg_get(seg, 0);
        fwrite(stptr, 1, len, fp);
    }
    return true;
}

int main()
{
    initProjPool();
    ProjectConsumer* con;
    addStream(con);
    sleep(INT32_MAX);
    removeStream(con);
    rlseProjPool();
    return 0;
}
