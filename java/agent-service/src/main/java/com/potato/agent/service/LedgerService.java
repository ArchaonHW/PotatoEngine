package com.potato.agent.service;

import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeLedger;

/**
 * AuditLedger session 管理——hash 鏈 append-only 帳本。
 * HeadHash 供 web3-service 取出做鏈上/外部錨定。
 */
@Service
public class LedgerService {

    private final Map<String, Long> sessions = new ConcurrentHashMap<>();

    public String create(String loadPath) {
        NativeJsonSupport.requireNative();
        long h = NativeLedger.nCreate(loadPath != null ? loadPath : "");
        if (h < 0) {
            throw new IllegalArgumentException(
                    "帳本載入驗證失敗——鏈斷點 seq=" + (-h - 1));
        }
        if (h == 0) {
            throw new IllegalStateException("ledger session 建立失敗");
        }
        String id = UUID.randomUUID().toString();
        sessions.put(id, h);
        return id;
    }

    public void destroy(String id) {
        Long h = sessions.remove(id);
        if (h != null) {
            NativeLedger.nDestroy(h);
        }
    }

    public String append(String id, String category, String eventType,
                         String payload, String provenance) {
        return NativeJsonSupport.passthrough(NativeLedger.nAppend(
                handle(id), category, eventType, payload, provenance));
    }

    public String appendHash(String id, String category, String eventType,
                             String payloadHashHex, String provenance) {
        return NativeJsonSupport.passthrough(NativeLedger.nAppendHash(
                handle(id), category, eventType, payloadHashHex, provenance));
    }

    public String verify(String id) {
        return NativeJsonSupport.passthrough(NativeLedger.nVerify(handle(id)));
    }

    public String headHash(String id) {
        String head = NativeLedger.nHeadHash(handle(id));
        return head != null ? head : "";
    }

    public long size(String id) {
        return NativeLedger.nSize(handle(id));
    }

    public String export(String id, int layer) {
        return NativeJsonSupport.passthrough(
                NativeLedger.nExport(handle(id), layer));
    }

    public boolean save(String id, String path) {
        return NativeLedger.nSave(handle(id), path);
    }

    private long handle(String id) {
        Long h = sessions.get(id);
        if (h == null) {
            throw new IllegalArgumentException("ledger session 不存在: " + id);
        }
        return h;
    }
}
