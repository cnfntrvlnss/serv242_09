/*************************************************************************
	> File Name: BufferProject_fork.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 16 Feb 2017 07:43:27 AM EST
 ************************************************************************/

#include <pthread.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include<iostream>
#include<map>
using namespace std;

map<unsigned long int, FILE*> g_allProjs;
string g_strSaveDir = "tmpData/";

FILE* getFile(unsigned long int pid)
{
    if(g_allProjs.find(pid) == g_allProjs.end()){
        char filename[512];
        sprintf(filename, "%s%lu.wav", g_strSaveDir.c_str(), pid);
        FILE *fp = fopen(filename, "wb");
        if(fp == NULL){
            fprintf(stderr, "failed to open file %s.\n", filename);
            return NULL;
        }
        g_allProjs[pid] = fp;
    }
    return g_allProjs[pid];
}

bool recvProjSegment(unsigned long int id, char *data, unsigned len)
{
    FILE *fp = getFile(id);
    if(fp == NULL) return false;
    size_t retw = fwrite(data, 1, len, fp);
    if(retw != len){
        fprintf(stderr, "failed to save data. error: %s.\n", strerror(errno));
    }
    return true;
}

void notifyProjFinish(unsigned long int pid)
{
    if(g_allProjs.find(pid) == g_allProjs.end()){
        fprintf(stderr, "PID=%lu before finishing it, no data arrives.\n", pid);
        return;
    }
    FILE *fp = g_allProjs[pid];
    fclose(fp);
    g_allProjs.erase(pid);
}

unsigned queryProjNum()
{
    return g_allProjs.size();
}
