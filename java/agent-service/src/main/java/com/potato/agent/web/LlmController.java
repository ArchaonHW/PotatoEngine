package com.potato.agent.web;

import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import com.potato.agent.service.LlmService;

/**
 * LLM 對話 API——body 的 config/messages 原樣送進原生端。
 * POST /api/llm/chat {"config":{provider,...}, "messages":[{role,content}]}
 */
@RestController
@RequestMapping("/api/llm")
public class LlmController {

    public record ChatRequest(Object config, Object messages) {
    }

    private final LlmService svc;

    public LlmController(LlmService svc) {
        this.svc = svc;
    }

    @PostMapping(value = "/chat", produces = MediaType.APPLICATION_JSON_VALUE)
    public String chat(@RequestBody ChatRequest req) {
        if (req.config() == null || req.messages() == null) {
            throw new IllegalArgumentException("需帶 config 與 messages");
        }
        return svc.chat(req.config(), req.messages());
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
