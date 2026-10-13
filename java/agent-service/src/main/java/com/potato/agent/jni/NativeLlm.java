package com.potato.agent.jni;

/**
 * LLM 對話 JNI 入口（無 session——每次呼叫依 config 建 client）。
 * configJson: {provider:"openai"|"anthropic"|"local", apiKey?, model?,
 *              modelPath?, baseURL?, temperature?, topP?, maxTokens?, stop?[]}
 * messagesJson: [{role:"system"|"user"|"assistant"|"tool", content, name?,
 *               toolCallId?, toolCalls?}]
 * 回傳: {success, content, finishReason, promptTokens, completionTokens,
 *        totalTokens, toolCalls[], error}
 */
public final class NativeLlm {

    private NativeLlm() {
    }

    public static native String nChat(String configJson, String messagesJson);

    /**
     * 多輪 tool-calling 迴圈：LLM 發 tool_calls → 引擎派發
     * 工具 → 結果回填 → 再送，直到純文字或達 maxRounds。
     * configJson 同 nChat，外加：
     *   tools?:[...]      顯式工具定義（缺省 = 旗標建的 registry 全部）
     *   zetaTools?:bool   掛 zeta_* 工具面
     *   renderTools?:bool 掛 render_* 工具面
     *   maxRounds?:int    預設 8
     * 回傳: {success, content, finishReason, rounds,
     *        messages:[{role,content,name?,toolCallId?,toolCalls?}], error}
     */
    public static native String nToolLoop(String configJson, String messagesJson);
}
