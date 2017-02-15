/*************************************************************************
    > File Name: myclient242.h
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Mon 25 Jul 2016 01:29:45 AM PDT
 ************************************************************************/

#ifndef MYCLIENT242_H
#define MYCLINET242_H

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct {
    char *base;
    size_t len;
} PckVec;

size_t writen(int fd, PckVec *vec, unsigned cnt, int *err, int istry);
size_t readn(int fd, PckVec *vec, unsigned cnt, int *err, int istry);

int cli_conn(const char *name, const char *addr);
void set_fl(int fd, int flags);
int serv_listen(const char *name);
int serv_accept(int listenfd, uid_t *uidptr);

#endif

