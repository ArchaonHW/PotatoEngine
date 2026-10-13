package com.potato.agent.service;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeChain;

/**
 * AgentChain 排程——spec JSON 透傳到原生端即時組裝執行。
 * renderTools=true 的 spec 會掛上 headless render_* 工具群。
 * 回傳原生端 JSON 字串原樣透傳。
 */
@Service
public class ChainService {

    public String run(Object spec) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(
                NativeChain.nRun(NativeJsonSupport.toJson(spec)));
    }

    // ---- 註冊鏈 ----

    public String register(Object spec) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(
                NativeChain.nRegister(NativeJsonSupport.toJson(spec)));
    }

    public String unregister(String name) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeChain.nUnregister(name));
    }

    public String list() {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeChain.nListChains());
    }

    public String runNamed(String name, Object inputs) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeChain.nRunNamed(name,
                NativeJsonSupport.toJson(
                        inputs != null ? inputs : java.util.Map.of())));
    }

    // ---- async（輪詢）----

    public String runAsync(Object spec) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(
                NativeChain.nRunAsync(NativeJsonSupport.toJson(spec)));
    }

    public String poll(long execId) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(NativeChain.nPoll(execId));
    }
}
