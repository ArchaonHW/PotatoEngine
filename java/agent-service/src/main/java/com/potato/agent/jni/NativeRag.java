package com.potato.agent.jni;

/**
 * RAG 知識庫 JNI 入口（session 制——每 handle 一套獨立向量庫）。
 * 同 handle 的呼叫由原生端全域鎖序列化。
 */
public final class NativeRag {

    private NativeRag() {
    }

    /** 建 session；回傳 >0 的 handle。 */
    public static native long nCreate();

    public static native void nDestroy(long handle);

    /**
     * cfgJson: {chunkSize?, chunkOverlap?, topK?, similarityThreshold?,
     *           strategy?:"semantic"|"keyword"|"hybrid"|"bm25",
     *           rerank?, enableCaching?, llm?{...同 NativeLlm config}}
     */
    public static native boolean nConfigure(long handle, String configJson);

    /** docJson: {id?, title, content, source?, url?, author?, tags?[]} */
    public static native boolean nAddDocument(long handle, String docJson);

    public static native boolean nRemoveDocument(long handle, String docId);

    /** 回傳 {success, chunks:[{id, content, source, score}]}。 */
    public static native String nRetrieve(long handle, String query, int topK);

    /** 回傳 {success, answer}。 */
    public static native String nGenerate(long handle, String query);

    /** 回傳 {success, documents, chunks, totalQueries, ...}。 */
    public static native String nStats(long handle);

    /**
     * 知識庫文件匯出到 JSON 檔（chunk/embedding 不落地,
     * import 時重建）。
     */
    public static native boolean nExport(long handle, String path);

    /** 自 nExport 產生的 JSON 檔匯入文件。 */
    public static native boolean nImport(long handle, String path);
}
