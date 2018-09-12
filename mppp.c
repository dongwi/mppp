typedef unsigned int UINT32;
typedef int INT32;
typedef int BOOL;
typedef unsigned char UINT8;
typedef char INT8

#define TRUE (1)
#define FALSE (0)

#define MPPP_DP_TX_LOCK(spin_lock) fpss_spin_lock(&spin_lock)
#define MPPP_DP_TX_UNLOCK(spin_lock) fpss_spin_unlock(&spin_lock)

#define MPPP_FLAG_INPUT_ORDER (1 << 0)
#define MPPP_FLAG_SHORT_SEQ (1 << 1)

#define MPPP_DP_HDR_FLAG_BEGIN (1 << 7)
#define MPPP_DP_HDR_FLAG_END   (1 << 6)

typedef struct MPPP_DP_ENTRY_STRUCT {
    UINT32 pppIndex;
    UINT32 cfgFlag;
    UINT32 seqNo;
    UINT32 sendWeight;
    fpss_spinlock_t sendLock;    
}MPPP_DP_ENTRY;

/**
 * mppp报文分片逻辑
 * @pMpppEntry 要发送报文的mppp接口
 * @ pPkt 要发送的报文，此报文为非成链报文
 * @ encapPkt 封装完成的报文链
 * @ fragWeight 报文的分片大小
 * @return 封装成功或失败
*/
static void mpppDpFwdTxEncapHdr
(
    MPPP_DP_ENTRY *pMpppEntry,
    UINT8 *sendPktData,
    nBuf_t *pOriPkt,
    BOOL endPkt
) {
    UINT32 seqNo = 0;
    INT8 *cp = sendPktData;
    UINT8 mpppHdrFlag = 0;

    if (cp[0] == 0xff) {
        cp += 2;
    } 

    if (cp[0] == 0x3d) {
        cp ++;
    } else {
        cp += 2;
    }

    seqNo = pMpppEntry->seqNo++;

    if (!(NBUF_DESC_GET_DESCFLAGS(pOriPkt) & DFP_DESC_DESCFLAGS_MPPP_FRAG_BEGIN)) {
        mpppHdrFlag |= MPPP_DP_HDR_FLAG_BEGIN;
        NBUF_DESC_SET_DESCFLAGS(pOriPkt, DFP_DESC_DESCFLAGS_MPPP_FRAG_BEGIN);
    }

    if (TRUE == endPkt) {
        mpppHdrFlag |= MPPP_DP_HDR_FLAG_END;
    }

    if (pMpppEntry->cfgFlag & MPPP_FLAG_SHORT_SEQ) {
        *cp++ = mpppHdrFlag | ((seqNo >> 8) & 0x0f);
        *cp++ = seqNo & 0xff;
    } else {
        *cp++ = mpppHdrFlag;
        *cp++ (seqNo >> 16) & 0xff;
        *cp++ (seqNo >> 8) & 0xff;
        *cp++ seqNo& 0xff;
    }

    return;
}

/**
 * mppp报文分片逻辑
 * @pMpppEntry 要发送报文的mppp接口
 * @ pPkt 要发送的报文，此报文为非成链报文
 * @ encapPkt 封装完成的报文链
 * @ fragWeight 报文的分片大小
 * @return 封装成功或失败
*/
static BOOL mpppDpFwdTxFragPkt
(
    MPPP_DP_ENTRY *pMpppEntry，
    nBuf_t *pPkt,
    nBuf_t **encapOutPkt,
    INT32 fragWeight
) {
    BOOL ret;
    INT8 linkHead[32] = {0};
    UINT32 pktLen
    UINT32 hdrLen;
    UINT32 linkType;
    INT8 *pData = NULl;
    nBuf_t *pktFrag, *pHeadTmp = NULL, *pHead = NULL;

	//提取报文的链路类型
    linkType = NBUF_DESC_GET_LINKTYPE(pPkt);
	//根据报文的链路类型，得到接口对应的链路头部
    ret = pppDpChanGetHdr(pMpppEntry->pppIndex, linkType, linkHead, &hdrLen);
    
    if (unlikely(ret == FALSE)) {
        *encapOutPkt = NULL;
        return FALSE;
    }

	//跳过链路头部的 ff 03
    hdrLen -= 2;
	
    //得到原始报文的长度
    pktLen = NBUF_DESC_GET_PKTLEN(pPkt);
	
	//调整原始报文的长度和数据指针
    NBUF_ADJUST_PKTLEN(pPkt, hdrLen);
	NBUF_ADJUST_PKTPTR(pPkt, -hdrLen);
    pktLen -= hdrLen;

    while (pktLen != 0) {
        fragWeight = MIN(fragWeight, pktLen);
        pktFrag = nBufLenAlloc(fragWeight + hdrLen + NBUF_PREDATA_SIZE + ADJ_LINKINFO_LEN);
        if (pktFrag == NULL) {
            *encapOutPkt = NULL;
            nBufChainFree(pHead);
            return FALSE;
        }

        pData = NBUF_DESC_GET_PDATA(pktFrag);
        NBUF_DESC_COPY(pktFrag, pPkt);//拷贝报文描述符，因此需要提前备份数据指针
        NBUF_DESC_SET_PDATA(pktFrag, pData);
        
        NBUF_DESC_CLR_DESCFLAGS(pktFrag, DFP_DESC_DESCFLAGS_MPPP_FRAG_BEGIN);//清楚报文描述符中的标记
        NBUF_SET_PKTNEXT(pktFrag, NULL);

        /**
         * pdata向后移除空余长度
         * 拷贝报文头部信息
         * 拷贝指定长度的报文信息
         * 设置报文长度
        */
        NBUF_ADJUST_PKTPTR(pktFrag, NBUF_PREDATA_SIZE);
        memcpy(NBUF_DESC_GET_PDATA(pktFrag), linkHead, hdrLen);
        memcpy(NBUF_DESC_GET_PDATA(pktFrag) + hdrLen, NBUF_DESC_GET_PDATA(pPkt), fragWeight);
        NBUF_DESC_SET_PKTLEN(pktFrag, fragWeight + hdrLen);

        //对原始报文进行调整
        NBUF_ADJUST_PKTPTR(pPkt, fragWeight);
        NBUF_ADJUST_PKTLEN(pPkt, -fragWeight);
        pktLen -= fragWeight;
        if (NULL == pHeadTmp) {
            pHead = pHeadTmp = pktFrag;
        } else {
            //内部已经将pHeadTmp指向了报文链的尾部
            NBUF_JOIN_TAIL(pktFrag, pHeadTmp);
        }
    }

    *encapOutPkt = pHead;
    return TRUE;
}

/**
 * mppp接口发送报文时的封装处理逻辑
 * @pMpppEntry 要发送报文的mppp接口
 * @ pPkt 要发送的报文，此报文为非成链报文
 * @ encapPkt 封装完成的报文链
 * @return 封装成功或失败
*/
static BOOL mpppDpFwdTxEncapPkt
(
    MPPP_DP_ENTRY *pMpppEntry,
    nBuf_t *pPkt,
    nBuf_t **encapPkt
) {
    UINT32 pktLen;
    BOOL isInputOrder;
    BOOL isFragPkt;
    BOOL isNeedSpinLock = TRUE;
    BOOL ret;
    nBuf_t *pHead, *pktBuf, *pNext;

    pktLen = NBUF_DESC_GET_PKTLEN(pPkt);
    
    fragWeight = pMpppEntry->sendWeight;
    
    isInputOrder = pMpppEntry->cfgFlag & MPPP_FLAG_INPUT_ORDER ? TRUE : FALSE;

    if (unlikely(pktLen <= fragWeight)) {
        pHead = pPkt;
        isFragPkt = FALSE:
    } else {
        isFragPkt = TRUE;
        ret = mpppDpFwdTxFragPkt(pMpppEntry, pPkt, &pHead, fragWeight);
    }

    if (isNeedSpinLock) {
        MPPP_DP_TX_LOCK(pMpppEntry->sendLock);
    }

    NBUF_CHAIN_FOR_EACH(pHead, pktBuf, pNext) {
        if (NBUF_GET_PKTNEXT(pktBuf) == NULL) {
            endPkt = TRUE;
        }
        mpppDpFwdTxEncapHdr(pMpppEntry, NBUF_DESC_GET_PDATA(pktBuf), pPkt, endPkt);
        NBUF_DESC_SET_DESCFLAGS(pktBuf, DFP_DESC_DESCFLAGS_MPPP_ENC);
    }

    if (isNeedSpinLock) {
        MPPP_DP_TX_UNLOCK(pMpppEntry->sendLock);
    }

end:
    if (TRUE == isFragPkt) {
        nBufFree(pPkt);
    }

    *encapPkt = pHead;

    return ret;
}