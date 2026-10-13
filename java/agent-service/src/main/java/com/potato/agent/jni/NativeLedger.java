package com.potato.agent.jni;

/**
 * AuditLedger JNI 入口（session 制——每 handle 一本 hash 鏈帳本）。
 * 供 web3-service 取 HeadHash 做外部錨定。
 */
public final class NativeLedger {

    private NativeLedger() {
    }

    /**
     * 建 session；loadPath 非空時載入既有 JSONL 帳本
     * （先驗鏈，失敗回傳 -(firstBadSeq+1)）。
     */
    public static native long nCreate(String loadPath);

    public static native void nDestroy(long handle);

    /**
     * 追加記錄（payload 由原生端雜湊,原文不落帳）。
     * 回傳 {success, seq, recordId, recordHash, prevHash, payloadHash,
     *       timestampUtcMs}
     */
    public static native String nAppend(long handle, String category,
                                        String eventType, String payload,
                                        String provenance);

    /** 同上但呼叫端已持有 payload 的 SHA-256（64 hex）。 */
    public static native String nAppendHash(long handle, String category,
                                            String eventType,
                                            String payloadHashHex,
                                            String provenance);

    /** 回傳 {success, passed, checked, firstBadSeq, reason}。 */
    public static native String nVerify(long handle);

    /** 鏈頭 hash——外部錨定值；空帳本回空字串，session 不存在回 null。 */
    public static native String nHeadHash(long handle);

    public static native long nSize(long handle);

    /** layer: 0=L0_Internal 1=L1_Deidentified 2=L2_Public → JSON 字串。 */
    public static native String nExport(long handle, int layer);

    public static native boolean nSave(long handle, String path);
}
