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
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import com.potato.agent.service.LedgerService;

/**
 * AuditLedger API——hash 鏈 append-only 帳本。
 * HeadHash（/head）供 web3-service 外部錨定。
 */
@RestController
@RequestMapping("/api/ledger")
public class LedgerController {

    public record CreateRequest(String loadPath) {
    }

    public record AppendRequest(String category, String eventType,
                                String payload, String payloadHash,
                                String provenance) {
    }

    public record SaveRequest(String path) {
    }

    private final LedgerService svc;

    public LedgerController(LedgerService svc) {
        this.svc = svc;
    }

    @PostMapping("/sessions")
    public Map<String, Object> create(
            @RequestBody(required = false) CreateRequest req) {
        return Map.of("id", svc.create(req != null ? req.loadPath() : null));
    }

    @DeleteMapping("/sessions/{id}")
    public Map<String, Object> destroy(@PathVariable String id) {
        svc.destroy(id);
        return Map.of("ok", true);
    }

    @PostMapping(value = "/sessions/{id}/records",
                 produces = MediaType.APPLICATION_JSON_VALUE)
    public String append(@PathVariable String id,
                         @RequestBody AppendRequest req) {
        if (req.payloadHash() != null && !req.payloadHash().isBlank()) {
            return svc.appendHash(id, req.category(), req.eventType(),
                    req.payloadHash(), req.provenance());
        }
        return svc.append(id, req.category(), req.eventType(),
                req.payload() != null ? req.payload() : "", req.provenance());
    }

    @GetMapping(value = "/sessions/{id}/verify",
                produces = MediaType.APPLICATION_JSON_VALUE)
    public String verify(@PathVariable String id) {
        return svc.verify(id);
    }

    @GetMapping("/sessions/{id}/head")
    public Map<String, Object> head(@PathVariable String id) {
        return Map.of("headHash", svc.headHash(id), "size", svc.size(id));
    }

    @GetMapping(value = "/sessions/{id}/export",
                produces = MediaType.APPLICATION_JSON_VALUE)
    public String export(@PathVariable String id,
                         @RequestParam(defaultValue = "2") int layer) {
        if (layer < 0 || layer > 2) {
            throw new IllegalArgumentException("layer 需為 0/1/2");
        }
        return svc.export(id, layer);
    }

    @PostMapping("/sessions/{id}/save")
    public Map<String, Object> save(@PathVariable String id,
                                    @RequestBody SaveRequest req) {
        return Map.of("ok", svc.save(id, req.path()));
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
