package com.potato.agent.web;

import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import com.potato.agent.service.RagService;

/**
 * RAG 知識庫 API——session 制,每 session 一套獨立向量庫。
 */
@RestController
@RequestMapping("/api/rag")
public class RagController {

    public record QueryRequest(String query, Integer topK) {
    }

    private final RagService svc;

    public RagController(RagService svc) {
        this.svc = svc;
    }

    @PostMapping("/sessions")
    public Map<String, Object> create() {
        return Map.of("id", svc.create());
    }

    @DeleteMapping("/sessions/{id}")
    public Map<String, Object> destroy(@PathVariable String id) {
        svc.destroy(id);
        return Map.of("ok", true);
    }

    @PostMapping("/sessions/{id}/config")
    public Map<String, Object> configure(@PathVariable String id,
                                         @RequestBody Object config) {
        return Map.of("ok", svc.configure(id, config));
    }

    @PostMapping("/sessions/{id}/documents")
    public Map<String, Object> addDocument(@PathVariable String id,
                                           @RequestBody Object doc) {
        return Map.of("ok", svc.addDocument(id, doc));
    }

    @DeleteMapping("/sessions/{id}/documents/{docId}")
    public Map<String, Object> removeDocument(@PathVariable String id,
                                              @PathVariable String docId) {
        return Map.of("ok", svc.removeDocument(id, docId));
    }

    @PostMapping(value = "/sessions/{id}/retrieve",
                 produces = MediaType.APPLICATION_JSON_VALUE)
    public String retrieve(@PathVariable String id,
                           @RequestBody QueryRequest req) {
        return svc.retrieve(id, req.query(),
                req.topK() != null ? req.topK() : 5);
    }

    @PostMapping(value = "/sessions/{id}/generate",
                 produces = MediaType.APPLICATION_JSON_VALUE)
    public String generate(@PathVariable String id,
                           @RequestBody QueryRequest req) {
        return svc.generate(id, req.query());
    }

    @GetMapping(value = "/sessions/{id}/stats",
                produces = MediaType.APPLICATION_JSON_VALUE)
    public String stats(@PathVariable String id) {
        return svc.stats(id);
    }

    public record PathRequest(String path) {
    }

    // 知識庫持久化——POST /sessions/{id}/export|import {"path":"..."}
    @PostMapping("/sessions/{id}/export")
    public Map<String, Object> export(@PathVariable String id,
                                      @RequestBody PathRequest req) {
        return Map.of("ok", svc.exportTo(id, req.path()));
    }

    @PostMapping("/sessions/{id}/import")
    public Map<String, Object> importFile(@PathVariable String id,
                                          @RequestBody PathRequest req) {
        return Map.of("ok", svc.importFrom(id, req.path()));
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
