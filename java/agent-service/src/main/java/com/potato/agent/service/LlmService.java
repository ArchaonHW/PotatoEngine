package com.potato.agent.service;

import org.springframework.stereotype.Service;

import com.potato.agent.jni.NativeLlm;

/**
 * LLM 對話——每次呼叫依 request 的 config 建原生 client
 * （OpenAI/Anthropic/Local）。金鑰由呼叫端帶入,服務不落盤。
 * 回傳原生端 JSON 字串原樣透傳。
 */
@Service
public class LlmService {

    public String chat(Object config, Object messages) {
        NativeJsonSupport.requireNative();
        return NativeJsonSupport.passthrough(
                NativeLlm.nChat(NativeJsonSupport.toJson(config),
                                NativeJsonSupport.toJson(messages)));
    }
}
