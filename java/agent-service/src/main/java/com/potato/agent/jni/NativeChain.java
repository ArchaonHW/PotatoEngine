package com.potato.agent.jni;

/**
 * AgentChain 排程 JNI 入口（無 session——spec 即時組裝並執行）。
 * specJson: {
 *   name?, inputs?:{k:v}, llm?:{...同 NativeLlm config}, renderTools?:bool,
 *   nodes:[{id, type:"agent"|"llm"|"tool"|"condition"|"loop"|"parallel",
 *           name?, agentId?, messages?[], llm?, tool?, params?,
 *           expr?, true?, false?, body?, maxIterations?, nodes?[],
 *           dependencies?[]}]
 * }
 * 回傳: {success, output, error, executionTime, nodes:{nodeId:output}}
 */
public final class NativeChain {

    private NativeChain() {
    }

    /** 即時組裝並同步執行——回傳 ChainResult JSON。 */
    public static native String nRun(String specJson);

    // ---- 註冊鏈（spec.name 命名,可重複執行）----

    /** 回傳 {success, name}；同名覆蓋。 */
    public static native String nRegister(String specJson);

    /** 回傳 {success, removed:0|1}。 */
    public static native String nUnregister(String name);

    /** 回傳 {chains:[name,...]}。 */
    public static native String nListChains();

    /** inputsJson: {k:v}——回傳 ChainResult JSON。 */
    public static native String nRunNamed(String name, String inputsJson);

    // ---- async 執行（輪詢模式）----

    /** 回傳 {success, execId}——背景執行緒跑鏈,節點事件入佇列。 */
    public static native String nRunAsync(String specJson);

    /**
     * 回傳 {done, events:[{node,output}], result:{...}|null}。
     * done=true 的那次 poll 之後 exec 自動回收。
     */
    public static native String nPoll(long execId);
}
