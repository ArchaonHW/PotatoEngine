package com.potato.web3.web;

import java.util.List;
import java.util.Map;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import com.potato.web3.service.AnchorService;
import com.potato.web3.service.AnchorService.AnchorRecord;
import com.potato.web3.service.WalletService;
import com.potato.web3.service.WalletService.WalletInfo;

/**
 * Web3 API——錢包/簽章/帳本錨定/agent DID。
 */
@RestController
@RequestMapping("/api/web3")
public class Web3Controller {

    public record SignRequest(String address, String message) {
    }

    public record VerifyRequest(String address, String message,
                                String signature) {
    }

    public record ImportRequest(String privateKey) {
    }

    public record AnchorRequest(String headHash, String source,
                                String ledgerSessionId) {
    }

    private final WalletService walletService;
    private final AnchorService anchorService;

    public Web3Controller(WalletService walletService,
                          AnchorService anchorService) {
        this.walletService = walletService;
        this.anchorService = anchorService;
    }

    @GetMapping("/status")
    public Map<String, Object> status() {
        return Map.of(
                "backend", anchorService.backend(),
                "serviceWallet", walletService.serviceWallet(),
                "wallets", "managed-in-memory");
    }

    // ---- 錢包 / DID ----

    /** 生成新錢包；回傳 {address, did, publicKey}（私鑰不回傳）。 */
    @PostMapping("/wallets")
    public WalletInfo createWallet() {
        return walletService.createWallet();
    }

    @PostMapping("/wallets/import")
    public WalletInfo importWallet(@RequestBody ImportRequest req) {
        return walletService.importKey(req.privateKey());
    }

    // ---- agent DID（engine agent 的鏈上身份）----

    public record AgentRequest(String name) {
    }

    /** 建具名 agent 錢包——DID = did:potato:<address>。 */
    @PostMapping("/agents")
    public WalletInfo createAgent(@RequestBody AgentRequest req) {
        return walletService.createAgent(req.name());
    }

    @GetMapping("/agents")
    public Map<String, Object> listAgents() {
        return walletService.listAgents();
    }

    /** DID/地址/name 解析——回傳錢包資訊（managed=本服務持有私鑰）。 */
    @GetMapping("/agents/{id}")
    public Map<String, Object> agentInfo(@PathVariable String id) {
        return walletService.agentInfo(id);
    }

    /** 以 agent 私鑰對訊息簽章——id 為 did/0x 地址/agent name。 */
    @PostMapping("/agents/{id}/sign")
    public Map<String, Object> agentSign(@PathVariable String id,
                                         @RequestBody SignRequest req) {
        return walletService.sign(id, req.message());
    }

    // ---- 簽章 ----

    /** personal_sign；address 省略或 "service" 用服務錢包。 */
    @PostMapping("/sign")
    public Map<String, Object> sign(@RequestBody SignRequest req) {
        return walletService.sign(req.address(), req.message());
    }

    /** ecrecover 驗章。 */
    @PostMapping("/verify")
    public Map<String, Object> verify(@RequestBody VerifyRequest req) {
        return walletService.verify(req.address(), req.message(),
                req.signature());
    }

    // ---- 錨定 ----

    /**
     * 錨定 headHash；或帶 ledgerSessionId 直接從 agent-service
     * 取鏈頭後錨定。
     */
    @PostMapping("/anchors")
    public AnchorRecord anchor(@RequestBody AnchorRequest req) {
        if (req.ledgerSessionId() != null
                && !req.ledgerSessionId().isBlank()) {
            return anchorService.anchorLedger(req.ledgerSessionId());
        }
        return anchorService.anchor(req.headHash(), req.source());
    }

    @GetMapping("/anchors")
    public List<AnchorRecord> anchors() {
        return anchorService.list();
    }

    @GetMapping("/anchors/{headHash}")
    public Map<String, Object> lookup(@PathVariable String headHash) {
        return anchorService.lookup(headHash);
    }

    @ExceptionHandler({IllegalArgumentException.class, IllegalStateException.class})
    public ResponseEntity<Map<String, Object>> badRequest(RuntimeException e) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST)
                .body(Map.of("error", e.getMessage()));
    }
}
