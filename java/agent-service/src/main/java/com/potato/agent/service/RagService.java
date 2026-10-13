package com.potato.agent.service;

import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeRag;

/**
 * RAG 知識庫 session 管理——每 session 對應原生端一套獨立向量庫。
 */
@Service
public class RagService {

    private final Map<String, Long> sessions = new ConcurrentHashMap<>();

    public String create() {
        NativeJsonSupport.requireNative();
        long h = NativeRag.nCreate();
        if (h <= 0) {
            throw new IllegalStateException("RAG session 建立失敗");
        }
        String id = UUID.randomUUID().toString();
        sessions.put(id, h);
        return id;
    }

    public void destroy(String id) {
        Long h = sessions.remove(id);
        if (h != null) {
            NativeRag.nDestroy(h);
        }
    }

    public boolean configure(String id, Object config) {
        return NativeRag.nConfigure(handle(id), NativeJsonSupport.toJson(config));
    }

    public boolean addDocument(String id, Object doc) {
        return NativeRag.nAddDocument(handle(id), NativeJsonSupport.toJson(doc));
    }

    public boolean removeDocument(String id, String docId) {
        return NativeRag.nRemoveDocument(handle(id), docId);
    }

    public String retrieve(String id, String query, int topK) {
        return NativeJsonSupport.passthrough(
                NativeRag.nRetrieve(handle(id), query, topK));
    }

    public String generate(String id, String query) {
        return NativeJsonSupport.passthrough(
                NativeRag.nGenerate(handle(id), query));
    }

    public String stats(String id) {
        return NativeJsonSupport.passthrough(NativeRag.nStats(handle(id)));
    }

    public boolean exportTo(String id, String path) {
        return NativeRag.nExport(handle(id), path);
    }

    public boolean importFrom(String id, String path) {
        return NativeRag.nImport(handle(id), path);
    }

    private long handle(String id) {
        Long h = sessions.get(id);
        if (h == null) {
            throw new IllegalArgumentException("rag session 不存在: " + id);
        }
        return h;
    }
}
