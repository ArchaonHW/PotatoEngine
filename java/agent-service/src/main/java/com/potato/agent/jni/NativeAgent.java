package com.potato.agent.jni;

/**
 * PotatoAgentJni.dll 的 JNI 入口。
 * 所有方法以 session handle（long）操作——handle 非執行緒安全，
 * 同 handle 的呼叫需由呼叫端（AgentSessionService）序列化。
 */
public final class NativeAgent {

    private static volatile boolean loaded = false;

    private NativeAgent() {
    }

    /** 載入原生 DLL；重複呼叫為 no-op。失敗拋 UnsatisfiedLinkError。 */
    public static synchronized void load(String dllPath) {
        if (loaded) {
            return;
        }
        System.load(dllPath);
        loaded = true;
    }

    public static boolean isLoaded() {
        return loaded;
    }

    /** 建 session；回傳 >0 的 handle，負值為錯誤碼。 */
    public static native long nCreate(String algo, int gridSize, boolean randMaps, long seed);

    public static native void nDestroy(long handle);

    /** 回傳 {x, y}；randMaps session 同時重抽地圖。 */
    public static native int[] nReset(long handle);

    /** 回傳 {w, h, goalX, goalY, nObs, ox1, oy1, ...}。 */
    public static native double[] nGrid(long handle);

    /** 目前觀測下 4 個動作的估值（Q 值或 policy 機率）。 */
    public static native double[] nActionValues(long handle);

    /**
     * agent 選動作→環境推進→可選學習更新。
     * 回傳 {x, y, rawReward, shapedReward, done, actionId, v0..v3}。
     */
    public static native double[] nStepAuto(long handle, boolean explore, boolean learn);

    /** episode 收尾：PG 更新 policy；QL/DQN epsilon decay。 */
    public static native void nEndEpisode(long handle, boolean learn);

    public static native void nSetExplorationRate(long handle, double rate);

    public static native double nGetExplorationRate(long handle);

    public static native boolean nSaveModel(long handle, String path);

    public static native boolean nLoadModel(long handle, String path);
}
