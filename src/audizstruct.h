/*************************************************************************
	> File Name: audizstruct.h
	> Author: 
	> Mail: 
	> Created Time: Wed 08 Feb 2017 09:59:16 PM EST
 ************************************************************************/

#ifndef _AUDIZSTRUCT_H
#define _AUDIZSTRUCT_H

#include <stdint.h>

struct Audiz_WaveUnit{
    unsigned long m_iPCBID; //节目ID, 长度8字节
    unsigned int m_iDataLen;//数据字节长度
    char *m_pData;//数据缓冲区
    void *m_pPCB;
};

#define AUDIZ_RESULT_MAX_NUM 10
struct Audiz_Result{
    unsigned int m_iTargetID; //目标ID
    unsigned int m_iAlarmType; //业务类型编码
    int m_iHarmLevel; //目标危害度
    float m_fLikely; //识别结果的置信度0~100
    float m_fTargetMatchLen; //匹配上的目标长度
    int m_iDataUnitNum; //结果中包含的片段数量1~MAX_DATAUNIT_NUM
    float m_fSegLikely[AUDIZ_RESULT_MAX_NUM]; //节目数据段(WavDataUnit) 与目标的相似度
    float m_fSegPosInPCB[AUDIZ_RESULT_MAX_NUM]; //节目数据段在节目中的时间位置
    float m_fSegPosInTarget[AUDIZ_RESULT_MAX_NUM]; //节目数据段在目标模板/配置项中的起始位置
    Audiz_WaveUnit *m_pDataUnit[AUDIZ_RESULT_MAX_NUM];
    char m_strInfo[1024]; //保留
};

#define SPKMDL_HDLEN 64
struct SpkMdlSt{
    char head[SPKMDL_HDLEN];
    unsigned len;
    char *buf;
};

#define AZ_CFGLINKNAME "cfg link"
#define AZ_DATALINKNAME "data link"
#define AZ_LINKBUILDOK "server ok"

#define AZOP_INVALID_MARK 0
#define AZOP_START_MARK 64
#define AZOP_QUERY_SAMPLE AZOP_START_MARK
#define AZOP_ADD_SAMPLE (AZOP_START_MARK + 2)
#define AZOP_DEL_SAMPLE (AZOP_START_MARK + 4)
#define AZOP_ADD_SAMPLEFILE (AZOP_START_MARK + 6)
#define AZOP_QUERY_PROJ (AZOP_START_MARK + 8)
#define AZOP_QUERY_SPACE (AZOP_START_MARK + 10)
//#define AZOP_WAIT_RESULT (AZOP_START_MARK + 12)
#define AZOP_REC_RESULT (AZOP_START_MARK + 13)

#define CHARS_AS_INIT32(chs) *(reinterpret_cast<int32_t*>(chs))

//used in p_client.
struct Audiz_Result_Head{
    int32_t type;
    int32_t ack;
    char *argBuf;
    
    bool isValid(){
        if(type == AZOP_QUERY_SAMPLE + 1){
            return true;
        }
        else if(type == AZOP_ADD_SAMPLE + 1){
            return true;
        }
        else if(type == AZOP_DEL_SAMPLE + 1){
            return true;
        }
        else if(type == AZOP_ADD_SAMPLEFILE + 1){
            return true;
        }
        else{
            return false;
        }

    }
    int getArgLen(){
        if(type == AZOP_QUERY_SAMPLE + 1){
            if(ack > 0) return ack * 64;
        }
        else if(type == AZOP_ADD_SAMPLE + 1){
            return 0;
        }
        else if(type == AZOP_DEL_SAMPLE + 1){
            return 0;
        }
        else if(type == AZOP_ADD_SAMPLEFILE + 1){
            return 0;
        }
    }
    void reset(){
        type = 0;
        ack = 0;
        argBuf = NULL;
    }

};

//used in server.
struct Audiz_PRequest_Head{
    int32_t type;
};
#endif
