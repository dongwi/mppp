typedef unsigned int UINT32;
typedef int INT32;
typedef int BOOL;

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

    linkType = NBUF_DESC_GET_LINKTYPE(pPkt);
    ret = pppDpChanGetHdr(pMpppEntry->pppIndex, linkType, linkHead, &hdrLen);
    
    if (unlikely(ret == FALSE)) {
        *encapOutPkt = NULL;
        return FALSE;
    }

    pktLen = NBUF_DESC_GET_PKTLEN(pPkt);
    
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