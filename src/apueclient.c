/*************************************************************************
    > File Name: apueclient.c
    > Author: zhengshurui
    > Mail:  zhengshurui@thinkit.cn
    > Created Time: Wed 24 Aug 2016 06:37:43 PM PDT
 ************************************************************************/
#include<stdio.h>
#include <stdlib.h>

#include"apueclient.h"

#if 0
#define OUTPUTE(fmt, ...) fprintf(stderr, "ERROR " fmt, ##__VA_ARGS__) 
#else
#define OUTPUTE(fmt, ...) 
#endif
#define offsetof(type, field) ((unsigned long)&((type *) 0)->field)
/**
 * do my best to write all data, or write nothing if istry is not zero,
 * the error status is returned through err.
 *return err: -1 if the link is broken, >0 means the count of trying.
 */
size_t writen(int fd, PckVec *vec, unsigned cnt, int *err, int istry)
{
    *err = 0;
    unsigned idx = 0;
    int nwrite = 0;
    size_t retn = 0;
    int curerr = 0;
    char *ptr;
    size_t nleft;
    unsigned trycnt = 0;
    float stepsecs = 0.000001;//wait for on usec if incompleted write happens.
    struct timeval timeout;
    for(idx =0; idx < cnt; idx++){
        ptr = vec[idx].base;
        nleft = vec[idx].len;
        while (nleft > 0){
            if((nwrite = write(fd, ptr, nleft)) < 0){
                curerr = errno;
                if(curerr == EAGAIN) {
                    if(trycnt != INT_MAX) trycnt ++;
                    if(istry > 0 && retn == 0){
                        break;   
                    }
                    timeout.tv_sec = 0;
                    timeout.tv_usec = stepsecs * 1000000;
                    select(0, NULL, NULL, NULL, &timeout);
                    continue;
                }
                else{
                    OUTPUTE( "error write. strerror: %s.\n", strerror(curerr));
                    *err = -1;
                    break;
                }
            }
            else if(nwrite == 0){
                OUTPUTE( "detect EOF while writing.\n");
                *err = -1;
                break;
            }
            nleft -= nwrite;
            ptr += nwrite;
            retn += nwrite;
        }

        if(istry > 0 && retn ==0) break;
        if(*err == -1){
            break;
        }
    }
    if(err ==0 && trycnt > 0){
        OUTPUTE( "for unavailable resource temporarily, try %u times by step of %f secs.\n", trycnt, stepsecs);
        *err = trycnt;
    }
    return retn;
}
/**
 * do my best to read all data requested. that is fill the buffer fully
 * , or read nothing if istry is greater than zero, otherwise fail meaning the link is broken. the error status is returned through err.
 * return err: -1 if the link is broken, >0 means the count of trying.
 */
size_t readn(int fd, PckVec *vec, unsigned cnt, int *err, int istry)
{
    *err = 0;
    int nread;
    unsigned idx = 0;
    int curerr = 0;
    size_t retn = 0;
    size_t nleft;
    char *ptr;
    struct timeval timeout;
    unsigned trycnt = 0;
    float secs = 0.000001;
    for(idx = 0; idx < cnt; idx ++){
        nleft = vec[idx].len;
        ptr = vec[idx].base;
        while(nleft > 0){
            if((nread = read(fd, ptr, nleft)) < 0){
                curerr = errno;
                if(curerr == EAGAIN){
                    if(trycnt != INT_MAX) trycnt ++;
                    if(istry > 0 && retn ==0) break;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = secs * 1000000;
                    select(0, NULL, NULL, NULL, &timeout);
                    continue;
                }
                else{
                    OUTPUTE( "error read. errno: %d.\n", curerr);
                    *err = -1;
                    break;
                }
            }
            else if(nread == 0){
                OUTPUTE( "detect EOF while reading.\n");
                *err = -1;
                break;
            }
            nleft -= nread;
            ptr += nread;
            retn += nread;
        }
        if(istry > 0 && retn ==0) break;
        if(*err == -1){
            break;
        }
    }
    if(err ==0 && trycnt > 0){
        OUTPUTE( "unavailable resource temporarily, try %u times by step of %f secs.\n", trycnt, secs);
        *err = trycnt;
    }
    return retn;
}

int cli_conn(const char *name, const char *addr)
{
    int fd, len, err, rval;
    struct sockaddr_un un, sun;
    int do_unlink = 0;

    if(strlen(addr) >= sizeof(un.sun_path)){
        errno = ENAMETOOLONG;
        return -1;
    }

    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) return -1;

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    sprintf(un.sun_path, "%s", name);
    len = offsetof(struct sockaddr_un, sun_path) + strlen(un.sun_path);
    
    if(access(un.sun_path, F_OK) == 0 && unlink(un.sun_path) == -1){
        rval = -1;
        goto errout;
    }
    if(bind(fd, (struct sockaddr*)&un, len) < 0){
        rval = -2;
        do_unlink = 1;
        goto errout;
    }

    if(chmod(un.sun_path, S_IRWXU) < 0){
        rval = -3;
        do_unlink = 1;
        goto errout;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, addr);
    len = offsetof(struct sockaddr_un, sun_path) + strlen(addr);
    if(connect(fd, (struct sockaddr*)&sun, len) < 0){
        rval = -4;
        do_unlink = 1;
        goto errout;
    }
    return fd;

errout:
    err = errno;
    close(fd);
    //if(do_unlink) unlink(un.sun_path);
    errno = err;
    return rval;
}


bool set_fl(int fd, int flags)
{
    int val;
    if((val = fcntl(fd, F_GETFL, 0)) < 0){
        OUTPUTE( "fcntl F_GETFL error.\n");
        return false;
    }
    val |= flags;
    if(fcntl(fd, F_SETFL, val) < 0){
        OUTPUTE( "fcntl F_SETFL error.\n");
        return false;
    }
    return true;
}

int serv_listen(const char *name)
{
    int fd, len, err, rval;
    struct sockaddr_un un;

    if(strlen(name) >= sizeof(un.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
        return -2;
    }

    unlink(name);
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strcpy(un.sun_path, name);
    len = offsetof(struct sockaddr_un, sun_path) + strlen(name);

    if(bind(fd, (struct sockaddr*)&un, len) < 0) {
        rval = -3;
        goto errout;
    }

    if(listen(fd, 3) < 0) {
        rval = -4;
        goto errout;
    }
    return fd;

errout:
    err = errno;
    close(fd);
    errno = err;
    return rval;
}

int serv_accept(int listenfd, uid_t *uidptr)
{
    int clifd, err, rval;
    socklen_t len;
    struct sockaddr_un un;
    struct stat statbuf;
    char *name;

    if((name = (char *)malloc(sizeof(un.sun_path) + 1)) == NULL) return -1;
    len = sizeof(un);
    if((clifd = accept(listenfd, (struct sockaddr*)&un, &len)) < 0){
        free(name);
        return -2;
    }
    
    len -= offsetof(struct sockaddr_un, sun_path);
    memcpy(name, un.sun_path, len);
    name[len] = 0;
    //OUTPUTE( "new client arrives, addr: %s.\n", name);
    if(stat(name, &statbuf) < 0){
        rval =-3;
        goto errout;
    }

#ifdef S_ISSOCK //not defined for SVR4
    if(S_ISSOCK(statbuf.st_mode) == 0) {
        rval = -4;
        goto errout;
    }
#endif

    if((statbuf.st_mode & (S_IRWXG | S_IRWXO)) ||
            (statbuf.st_mode & S_IRWXU) != S_IRWXU) {
        rval = -5;
        goto errout;
    }
    /*
    staletime = time(NULL) - STALE;
    if(statbuf.st_atime < staletime ||
            statbuf.st_ctime < staletime ||
            statbuf.st_mtime < staletime){
        rval = -6; //i-node is too old.
        goto errout;
    }*/

    if(uidptr != NULL){
        *uidptr  = statbuf.st_uid;
    }
    free(name);
    return (clifd);
errout:
    err = errno;
    close(clifd);
    free(name);
    errno = err;
    return rval;
}

