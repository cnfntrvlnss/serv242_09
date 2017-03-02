/*************************************************************************
	> File Name: /home/zheng/Desktop/serv242_09/src/audiz/samplelib.h
	> Author: 
	> Mail: 
	> Created Time: Thu 02 Mar 2017 01:02:51 AM EST
 ************************************************************************/

#ifndef _AUDIZ_SAMPLELIB_H
#define _AUDIZ_SAMPLELIB_H
namespace audiz{

void initSampleLib();
bool storeSample(const char *head, char *data, unsigned len);
void finishStore();
void addSample(const char *head, char* data, unsigned len);
void rmSample(const char *head);


void rmSample(const char *head);
struct SampleConsumer{
    //TODO for consuming samples
    virtual bool addOne(const char* smpHead) =0;
    virtual bool rmOne(const char* smpHead) =0;
    virtual void feedAll() =0;
    virtual ~SampleConsumer(){
    }
};

void addSmpConsumer(SampleConsumer* cmr);
void rmSmpConsumer(SampleConsumer* cmr);

}
#endif
