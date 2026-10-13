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

import com.potato.agent.service.ChainService;

/**
 * AgentChain 排程 API——request body 即 chain spec，
 * 原樣送原生端執行,回傳最終輸出與逐節點結果。
 * POST /api/chain/run {name?, inputs?, llm?, renderTools?, nodes:[...]}
 */
@RestController
@RequestMapping("/api/chain")
public class ChainController {

    private final ChainService svc;

    public ChainController(ChainService svc) {
        this.svc = svc;
    }

    @PostMapping(value = "/run", produces = MediaType.APPLICATION_JSON_VALUE)
    public String run(@RequestBody Object spec) {
        if (spec == null) {
            throw new IllegalArgumentException("需帶 chain spec");
        }
        return svc.run(spec);
    }

    // ---- 註冊鏈：POST /chains 註冊,GET /chains 清單,
    //      DELETE /chains/{name} 移除,POST /chains/{name}/run 依名執行 ----

    @PostMapping(value = "/chains", produces = MediaType.APPLICATION_JSON_VALUE)
    public String register(@RequestBody Object spec) {
        if (spec == null) {
            throw new IllegalArgumentException("需帶 chain spec（含 name）");
        }
        return svc.register(spec);
    }

    @GetMapping(value = "/chains", produces = MediaType.APPLICATION_JSON_VALUE)
    public String list() {
        return svc.list();
    }

    @DeleteMapping(value = "/chains/{name}",
                   produces = MediaType.APPLICATION_JSON_VALUE)
    public String unregister(@PathVariable String name) {
        return svc.unregister(name);
    }

    @PostMapping(value = "/chains/{name}/run",
                 produces = MediaType.APPLICATION_JSON_VALUE)
    public String runNamed(@PathVariable String name,
                           @RequestBody(required = false) Object inputs) {
        return svc.runNamed(name, inputs);
    }

    // ---- async：POST /async 起背景執行（body 同 /run spec）,
    //      GET /async/{execId} 輪詢節點事件與最終結果 ----

    @PostMapping(value = "/async", produces = MediaType.APPLICATION_JSON_VALUE)
    public String runAsync(@RequestBody Object spec) {
        if (spec == null) {
            throw new IllegalArgumentException("需帶 chain spec");
        }
        return svc.runAsync(spec);
    }

    @GetMapping(value = "/async/{execId}",
                produces = MediaType.APPLICATION_JSON_VALUE)
    public String poll(@PathVariable long execId) {
        return svc.poll(execId);
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
