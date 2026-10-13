package com.potato.web3.service;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicLong;

import com.fasterxml.jackson.databind.ObjectMapper;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.cloud.client.ServiceInstance;
import org.springframework.cloud.client.loadbalancer.LoadBalancerClient;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;

import com.potato.web3.crypto.EthereumCrypto;
import com.potato.web3.crypto.EthereumCrypto.KeyPair;
import com.potato.web3.crypto.EthereumCrypto.Signature;
import com.potato.web3.crypto.EvmRawTx;

/**
 * AuditLedger HeadHash 外部錨定。
 * - backend=local：服務錢包對 headHash 做 personal_sign,
 *   收據寫進 anchor-file（JSONL）——AuditLedger 文件所述
 *   「定期外部錨定」的離線實作,日後可用收據重算比對
 * - backend=evm：把 headHash 當 tx data 送 EVM 鏈
 *   （legacy EIP-155 自簽 tx,eth_sendRawTransaction）
 */
@Service
public class AnchorService {

    public record AnchorRecord(long seq, String timestamp, String headHash,
                               String source, String backend,
                               String signer, String proof) {
    }

    private static final ObjectMapper MAPPER = new ObjectMapper();

    private final WalletService walletService;
    private final LoadBalancerClient loadBalancer;

    private final List<AnchorRecord> anchors = new CopyOnWriteArrayList<>();
    private final AtomicLong seq = new AtomicLong(1);

    @Value("${potato.web3.backend:local}")
    private String backend;
    @Value("${potato.web3.rpc-url:}")
    private String rpcUrl;
    @Value("${potato.web3.chain-id:1}")
    private long chainId;
    @Value("${potato.web3.anchor-file:web3-anchors.jsonl}")
    private String anchorFile;
    @Value("${potato.web3.agent-service-url:http://agent-service}")
    private String agentServiceUrl;

    public AnchorService(WalletService walletService,
                         LoadBalancerClient loadBalancer) {
        this.walletService = walletService;
        this.loadBalancer = loadBalancer;
    }

    public String backend() {
        return backend;
    }

    /**
     * 錨定一個 64-hex 的 headHash。
     * source 為來源說明（如 "agent-service/ledger/{id}"）。
     */
    public AnchorRecord anchor(String headHash, String source) {
        if (headHash == null || !headHash.matches("(?i)[0-9a-f]{64}")) {
            throw new IllegalArgumentException("headHash 需為 64 hex");
        }
        String proof;
        if ("evm".equalsIgnoreCase(backend)) {
            proof = anchorOnChain(headHash);
        } else {
            proof = signLocally(headHash);
        }
        AnchorRecord rec = new AnchorRecord(
                seq.getAndIncrement(),
                Instant.now().toString(),
                headHash.toLowerCase(),
                source != null ? source : "",
                backend,
                walletService.serviceWallet().address(),
                proof);
        anchors.add(rec);
        persist(rec);
        return rec;
    }

    /**
     * 經 Eureka 呼叫 agent-service 取 ledger headHash 再錨定——
     * 一條呼叫完成「取鏈頭 → 外部錨定」。
     */
    public AnchorRecord anchorLedger(String sessionId) {
        // 經 Eureka 解析 agent-service 實例；失敗回落到設定位址
        String base = agentServiceUrl;
        ServiceInstance inst = loadBalancer.choose("agent-service");
        if (inst != null) {
            base = inst.getUri().toString();
        }
        // RestClient 走 Boot 4 的 Jackson 3 converter——吃 Map 不吃
        // Jackson 2 的 JsonNode
        Map<?, ?> head;
        try {
            head = RestClient.create(base)
                    .get()
                    .uri("/api/ledger/sessions/" + sessionId + "/head")
                    .retrieve()
                    .body(Map.class);
        } catch (Exception e) {
            throw new IllegalStateException(
                    "取 agent-service headHash 失敗: " + e.getMessage());
        }
        if (head == null || head.get("headHash") == null) {
            throw new IllegalStateException("agent-service 回應缺 headHash");
        }
        return anchor(String.valueOf(head.get("headHash")),
                "agent-service/ledger/" + sessionId);
    }

    public List<AnchorRecord> list() {
        return List.copyOf(anchors);
    }

    /** 查某 headHash 是否已錨定。 */
    public Map<String, Object> lookup(String headHash) {
        for (AnchorRecord r : anchors) {
            if (r.headHash().equalsIgnoreCase(headHash)) {
                return Map.of("anchored", true, "anchor", r);
            }
        }
        return Map.of("anchored", false);
    }

    // ---- 後端實作 ----

    /** local：personal_sign(headHash) → 收據即證明。 */
    private String signLocally(String headHash) {
        KeyPair kp = walletService.serviceKeyPair();
        byte[] hash = EthereumCrypto.personalMessageHash(
                ("potato-ledger-anchor:" + headHash)
                        .getBytes(StandardCharsets.UTF_8));
        Signature sig = EthereumCrypto.sign(kp.privateKey(), hash);
        return "sig:" + sig.toHex65();
    }

    /** evm：data=0x+"f0a7a7c0"(magic)+headHash 的 self-tx 上鏈。 */
    private String anchorOnChain(String headHash) {
        if (rpcUrl == null || rpcUrl.isBlank()) {
            throw new IllegalStateException(
                    "backend=evm 但未設定 potato.web3.rpc-url");
        }
        EvmRpcClient rpc = new EvmRpcClient(rpcUrl);
        KeyPair kp = walletService.serviceKeyPair();
        String address = kp.address();

        byte[] data = new byte[36];
        data[0] = (byte) 0xF0; data[1] = (byte) 0xA7;
        data[2] = (byte) 0xA7; data[3] = (byte) 0xC0;  // magic "potato anchor"
        System.arraycopy(EthereumCrypto.Hex.decode(headHash), 0,
                data, 4, 32);

        BigInteger nonce = rpc.nonce(address);
        BigInteger gasPrice = rpc.gasPrice();
        byte[] signed = EvmRawTx.sign(
                nonce, gasPrice, BigInteger.valueOf(50000),
                EthereumCrypto.Hex.decode(address), BigInteger.ZERO, data,
                BigInteger.valueOf(chainId), kp.privateKey());
        return "tx:" + rpc.sendRawTransaction(signed);
    }

    private void persist(AnchorRecord rec) {
        try {
            String line = MAPPER.writeValueAsString(rec) + "\n";
            Files.writeString(Path.of(anchorFile), line,
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (Exception e) {
            // 檔案寫不進去不影響記憶體內錨定結果——只不過失去持久化
            org.slf4j.LoggerFactory.getLogger(AnchorService.class)
                    .warn("anchor 收據落盤失敗: {}", e.getMessage());
        }
    }
}
