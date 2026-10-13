package com.potato.agent.web;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.webmvc.test.autoconfigure.WebMvcTest;
import org.springframework.http.MediaType;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.web.servlet.MockMvc;

import com.potato.agent.service.ChainService;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.delete;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

/**
 * /api/chain 契約切片——run/註冊鏈/async 三組端點。
 */
@WebMvcTest(ChainController.class)
class ChainControllerTest {

    @Autowired
    private MockMvc mvc;

    @MockitoBean
    private ChainService svc;

    @Test
    void runPassthrough() throws Exception {
        String result = "{\"success\":true,\"output\":\"ok\",\"nodes\":{\"a\":\"ok\"}}";
        when(svc.run(any())).thenReturn(result);
        mvc.perform(post("/api/chain/run")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"nodes\":[{\"id\":\"a\",\"type\":\"tool\",\"tool\":\"t\"}]}"))
                .andExpect(status().isOk())
                .andExpect(content().string(result));
    }

    @Test
    void registerAndListAndRunNamed() throws Exception {
        when(svc.register(any())).thenReturn("{\"success\":true,\"name\":\"c1\"}");
        when(svc.list()).thenReturn("{\"chains\":[\"c1\"]}");
        when(svc.runNamed(eq("c1"), any()))
                .thenReturn("{\"success\":true,\"output\":\"done\"}");

        mvc.perform(post("/api/chain/chains")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"name\":\"c1\",\"nodes\":[]}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.name").value("c1"));

        mvc.perform(get("/api/chain/chains"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.chains[0]").value("c1"));

        mvc.perform(post("/api/chain/chains/c1/run")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"x\":\"1\"}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.output").value("done"));
    }

    @Test
    void asyncStartAndPoll() throws Exception {
        when(svc.runAsync(any())).thenReturn("{\"success\":true,\"execId\":7}");
        when(svc.poll(7)).thenReturn(
                "{\"done\":true,\"events\":[{\"node\":\"a\",\"output\":\"ok\"}],"
                + "\"result\":{\"success\":true}}");

        mvc.perform(post("/api/chain/async")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"nodes\":[]}"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.execId").value(7));

        mvc.perform(get("/api/chain/async/7"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.done").value(true))
                .andExpect(jsonPath("$.events[0].node").value("a"));
    }

    @Test
    void unregisterRemoves() throws Exception {
        when(svc.unregister("c1")).thenReturn("{\"success\":true,\"removed\":1}");
        mvc.perform(delete("/api/chain/chains/c1"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.removed").value(1));
    }
}
