package com.potato.agent.web;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.webmvc.test.autoconfigure.WebMvcTest;
import org.springframework.http.MediaType;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;

import com.potato.agent.service.LlmService;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

/**
 * /api/llm 契約切片——原生 JSON 字串原樣透傳,不經二次序列化。
 */
@WebMvcTest(LlmController.class)
class LlmControllerTest {

    @Autowired
    private MockMvc mvc;

    @MockitoBean
    private LlmService svc;

    @Test
    void chatPassthroughNativeJson() throws Exception {
        String nativeJson = "{\"success\":true,\"content\":\"hi\",\"totalTokens\":3}";
        when(svc.chat(any(), any())).thenReturn(nativeJson);
        mvc.perform(post("/api/llm/chat")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"config\":{\"provider\":\"local\"},"
                                + "\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}"))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(content().string(nativeJson));
    }

    @Test
    void missingFieldsBecomes400() throws Exception {
        mvc.perform(post("/api/llm/chat")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"config\":{}}"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.error").exists());
    }

    @Test
    void nativeNotLoadedBecomes400() throws Exception {
        when(svc.chat(any(), any()))
                .thenThrow(new IllegalStateException("PotatoAgentJni.dll 未載入"));
        mvc.perform(post("/api/llm/chat")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"config\":{},\"messages\":[]}"))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.error").value("PotatoAgentJni.dll 未載入"));
    }
}
