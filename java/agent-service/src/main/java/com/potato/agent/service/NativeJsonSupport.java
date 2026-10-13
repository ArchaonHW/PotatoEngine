package com.potato.agent.service;

import com.fasterxml.jackson.databind.ObjectMapper;

import com.potato.agent.jni.NativeAgent;

/**
 * JNI JSON 橋的共用工具：ObjectMapper 序列化/解析 + 原生庫就緒檢查。
 * 引擎側 JSON 往返一律字串——此處在 Java 邊界做型別還原。
 */
final class NativeJsonSupport {

    static final ObjectMapper MAPPER = new ObjectMapper();

    private NativeJsonSupport() {
    }

    static void requireNative() {
        if (!NativeAgent.isLoaded()) {
            throw new IllegalStateException("PotatoAgentJni.dll 未載入");
        }
    }

    static String toJson(Object value) {
        try {
            return MAPPER.writeValueAsString(value);
        } catch (Exception e) {
            throw new IllegalArgumentException("JSON 序列化失敗: " + e.getMessage());
        }
    }

    /**
     * 原生端回傳的 JSON 字串原樣透傳——controller 端以
     * produces=application/json 直接寫回,不再經 JsonNode 二次
     * 序列化（Boot 4 HTTP converter 是 Jackson 3,不認得
     * 此處的 Jackson 2 JsonNode）。
     */
    static String passthrough(String json) {
        return json != null ? json : "{}";
    }
}
