/*************************************************************************
	> File Name: audizserv_c.cpp
	> Author: 
	> Mail: 
	> Created Time: Thu 23 Feb 2017 03:45:08 AM EST
 ************************************************************************/

#include<iostream>

#include "../audizrecst.h"
#include "../apueclient.h"
#include "ProjectBuffer.h"

using namespace std;
using namespace audiz;

class RecSession: public ProjectConsumer{
public:
    int link;

    void sendProject(Project *proj){
        //TODO serialize data of Project as package, and put package in link.
    }

    //wait for link read.
    void recvProject()
    {
        Project *proj = NULL;
        Audiz_Result *res = NULL;
        //TODO recv(link, ...);
        //TODO parse Result.
        

        confirm(proj, res);
    }

};


/*
void run(){
    RecSession rec1, rec2;
    while(true){
        if(rec1.fail()){
            rec1.connect();
        }
        if(rec2.fail()){
            rec2.connect();
        }

        pollfd fds[2];
        fds[0].fd = rec1.link;
        fds[0].events = POLLIN;
        fds[1].fd = rec2.link;
        fds[1].events = POLLIN;
        int retp = poll(fds, 1, -1);
        if(retp > 0){
            res = readResult();
            que.confirm(proj, res);
        }
    }
}
*/
