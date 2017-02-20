/*************************************************************************
	> File Name: /home/zheng/Desktop/serv242_09/src/audiz/BufferProject_fork.h
	> Author: 
	> Mail: 
	> Created Time: Thu 16 Feb 2017 08:42:31 PM EST
 ************************************************************************/

#ifndef BUFFERPROJECT_FORK_H
#define BUFFERPROJECT_FORK_H

bool recvProjSegment(unsigned long int id, char *data, unsigned len);
void notifyProjFinish(unsigned long int pid);
unsigned queryProjNum();
#endif
