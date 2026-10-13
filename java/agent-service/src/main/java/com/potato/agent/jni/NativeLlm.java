package com.potato.agent.jni;

/**
 * LLM 對話 JNI 入口（無 session——每次呼叫依 config 建 client）。
 * configJson: {provider:"openai"|"anthropic"|"local", apiKey?, model?,
 *              modelPath?, baseURL?, temperature?, topP?, maxTokens?, stop?[]}
 * messagesJson: [{role:"system"|"user"|"assistant"|"tool", content, name?}]
 * 回傳: {success, content, finishReason, promptTokens, completionTokens,
 *        totalTokens, error}
 */
public final class NativeLlm {

    private NativeLlm() {
    }

    public static native String nChat(String configJson, String messagesJson);
}
