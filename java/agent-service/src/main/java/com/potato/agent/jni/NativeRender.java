package com.potato.agent.jni;

/**
 * Agent 繪圖 JNI 入口——全域唯一 headless RenderingServer
 * （Godot singleton 語義）+ render_* 工具群。
 * 所有呼叫由原生端全域鎖序列化。
 */
public final class NativeRender {

    private NativeRender() {
    }

    /** 初始化 server + 註冊工具；回傳工具數，<0 失敗。冪等。 */
    public static native int nInit();

    public static native void nShutdown();

    /**
     * 呼叫 render_* 工具；paramsJson 為 {name:value} string map
     * （RID 以 uint64 字串往返）。
     * 回傳 {success, output, error, executionTime, metadata}
     */
    public static native String nInvoke(String toolName, String paramsJson);

    /** 回傳 [{name, description, parameters:{name:desc}}]。 */
    public static native String nListTools();

    /** 回傳 {success, canvasItems, sceneInstances, drawCalls, frames}。 */
    public static native String nStats();
}
