package com.potato.agent.web;

import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import com.potato.agent.service.RenderService;

/**
 * Agent 繪圖 API——headless RenderingServer + render_* 工具群。
 * agent 用 invoke 逐步組裝場景（RID 以 uint64 字串往返）。
 */
@RestController
@RequestMapping("/api/render")
public class RenderController {

    public record InvokeRequest(String tool, Object params) {
    }

    private final RenderService svc;

    public RenderController(RenderService svc) {
        this.svc = svc;
    }

    @PostMapping("/init")
    public Map<String, Object> init() {
        return Map.of("ok", true, "tools", svc.init());
    }

    @PostMapping("/shutdown")
    public Map<String, Object> shutdown() {
        svc.shutdown();
        return Map.of("ok", true);
    }

    @GetMapping(value = "/tools", produces = MediaType.APPLICATION_JSON_VALUE)
    public String tools() {
        return svc.listTools();
    }

    @PostMapping(value = "/invoke", produces = MediaType.APPLICATION_JSON_VALUE)
    public String invoke(@RequestBody InvokeRequest req) {
        if (req.tool() == null || req.tool().isBlank()) {
            throw new IllegalArgumentException("需帶 tool 名稱");
        }
        return svc.invoke(req.tool(),
                req.params() != null ? req.params() : Map.of());
    }

    @GetMapping(value = "/stats", produces = MediaType.APPLICATION_JSON_VALUE)
    public String stats() {
        return svc.stats();
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
