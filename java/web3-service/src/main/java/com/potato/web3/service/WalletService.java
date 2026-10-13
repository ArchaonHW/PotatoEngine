package com.potato.web3.service;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import com.potato.web3.crypto.EthereumCrypto;
import com.potato.web3.crypto.EthereumCrypto.KeyPair;
import com.potato.web3.crypto.EthereumCrypto.Signature;

import jakarta.annotation.PostConstruct;

/**
 * secp256k1 錢包 + Ethereum personal_sign 簽章 + agent DID 註冊。
 * - createWallet/createAgent：生成 keypair,DID 為 did:potato:<address>
 * - agent = 具名受管錢包——engine agent 的鏈上身份載體,
 *   其行為（ledger 事件、chain 輸出）可用 agent 私鑰簽章
 * - sign/verify：personal_sign 簽名 + ecrecover 驗章
 * - 持久化：wallet-file 落盤（AES-GCM 加密私鑰,passphrase
 *   未設定時存明文 hex + WARN——開發模式；正式環境接 KMS/HSM）
 */
@Service
public class WalletService {

    private static final Logger log = LoggerFactory.getLogger(WalletService.class);

    public record WalletInfo(String address, String did, String publicKey) {
    }

    private static final ObjectMapper MAPPER = new ObjectMapper();
    private static final SecureRandom RNG = new SecureRandom();
    private static final int GCM_TAG_BITS = 128;
    private static final int GCM_IV_LEN = 12;

    private final Map<String, KeyPair> wallets = new ConcurrentHashMap<>();
    private final Map<String, String> agents = new ConcurrentHashMap<>();
    private KeyPair serviceWallet;

    @Value("${potato.web3.private-key:}")
    private String configuredKey;
    @Value("${potato.web3.wallet-file:web3-wallets.json}")
    private String walletFile;
    @Value("${potato.web3.wallet-passphrase:}")
    private String passphrase;

    @PostConstruct
    void init() {
        if (!tryLoad()) {
            serviceWallet = configuredKey != null && !configuredKey.isBlank()
                    ? EthereumCrypto.fromPrivateKey(
                            new BigInteger(strip0x(configuredKey), 16))
                    : EthereumCrypto.generateKeyPair();
            persist();
        }
        log.info("web3 service wallet: {} (wallets={}, agents={})",
                serviceWallet.address(), wallets.size(), agents.size());
        if (passphrase == null || passphrase.isBlank()) {
            log.warn("potato.web3.wallet-passphrase 未設定——"
                    + "私鑰以明文 hex 存 {}（僅限開發環境）", walletFile);
        }
    }

    // ---- 錢包檔持久化 ----

    private boolean tryLoad() {
        Path p = Path.of(walletFile);
        if (!Files.exists(p)) return false;
        try {
            JsonNode j = MAPPER.readTree(Files.readString(p));
            String enc = j.path("enc").asText("plain");
            serviceWallet = EthereumCrypto.fromPrivateKey(
                    new BigInteger(decrypt(
                            j.path("service").asText(), enc), 16));
            for (var e : j.path("wallets").properties()) {
                wallets.put(e.getKey(), EthereumCrypto.fromPrivateKey(
                        new BigInteger(decrypt(e.getValue().asText(), enc), 16)));
            }
            for (var e : j.path("agents").properties()) {
                agents.put(e.getKey(), e.getValue().asText());
            }
            return serviceWallet != null;
        } catch (Exception e) {
            log.warn("錢包檔讀取失敗,重新生成: {}", e.getMessage());
            wallets.clear();
            agents.clear();
            return false;
        }
    }

    private synchronized void persist() {
        if (walletFile == null || walletFile.isBlank()) return;
        try {
            String enc = (passphrase != null && !passphrase.isBlank())
                    ? "aes-gcm" : "plain";
            ObjectNode j = MAPPER.createObjectNode();
            j.put("enc", enc);
            j.put("service", encrypt(serviceWallet.privateKeyHex(), enc));
            ObjectNode w = j.putObject("wallets");
            wallets.forEach((addr, kp) ->
                    w.put(addr, encrypt(kp.privateKeyHex(), enc)));
            ObjectNode a = j.putObject("agents");
            agents.forEach(a::put);
            Path tmp = Path.of(walletFile + ".tmp");
            Files.writeString(tmp, MAPPER.writeValueAsString(j));
            Files.move(tmp, Path.of(walletFile),
                    java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception e) {
            log.warn("錢包檔寫入失敗: {}", e.getMessage());
        }
    }

    /** AES-GCM(SHA-256(passphrase)) 加密 hex 私鑰→base64(iv‖ct)。 */
    private String encrypt(String plainHex, String enc) {
        if (!"aes-gcm".equals(enc)) return plainHex;
        try {
            byte[] iv = new byte[GCM_IV_LEN];
            RNG.nextBytes(iv);
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.ENCRYPT_MODE, key(), new GCMParameterSpec(GCM_TAG_BITS, iv));
            byte[] ct = c.doFinal(plainHex.getBytes(StandardCharsets.UTF_8));
            byte[] blob = new byte[GCM_IV_LEN + ct.length];
            System.arraycopy(iv, 0, blob, 0, GCM_IV_LEN);
            System.arraycopy(ct, 0, blob, GCM_IV_LEN, ct.length);
            return Base64.getEncoder().encodeToString(blob);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException("AES-GCM 加密失敗", e);
        }
    }

    private String decrypt(String stored, String enc) {
        if (!"aes-gcm".equals(enc)) return stored;
        try {
            byte[] blob = Base64.getDecoder().decode(stored);
            byte[] iv = new byte[GCM_IV_LEN];
            System.arraycopy(blob, 0, iv, 0, GCM_IV_LEN);
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(GCM_TAG_BITS, iv));
            byte[] ct = new byte[blob.length - GCM_IV_LEN];
            System.arraycopy(blob, GCM_IV_LEN, ct, 0, ct.length);
            return new String(c.doFinal(ct), StandardCharsets.UTF_8);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException("AES-GCM 解密失敗", e);
        }
    }

    private SecretKeySpec key() throws NoSuchAlgorithmException {
        byte[] k = MessageDigest.getInstance("SHA-256")
                .digest(passphrase.getBytes(StandardCharsets.UTF_8));
        return new SecretKeySpec(k, "AES");
    }

    // ---- 錢包 ----

    /** 服務錢包——錨定交易的簽署者。 */
    public WalletInfo serviceWallet() {
        return info(serviceWallet);
    }

    /** 服務錢包金鑰物件（供 AnchorService 簽章,不轉 hex 暴露）。 */
    public KeyPair serviceKeyPair() {
        return serviceWallet;
    }

    public WalletInfo createWallet() {
        KeyPair kp = EthereumCrypto.generateKeyPair();
        wallets.put(kp.address().toLowerCase(), kp);
        persist();
        return info(kp);
    }

    /** 註冊外部私鑰成為受管錢包（開發用）。 */
    public WalletInfo importKey(String privateKeyHex) {
        KeyPair kp = EthereumCrypto.fromPrivateKey(
                new BigInteger(strip0x(privateKeyHex), 16));
        wallets.put(kp.address().toLowerCase(), kp);
        persist();
        return info(kp);
    }

    // ---- agent DID 註冊 ----

    /** 具名 agent 錢包——DID = did:potato:<address>,name 可重複覆蓋。 */
    public WalletInfo createAgent(String name) {
        if (name == null || name.isBlank()) {
            throw new IllegalArgumentException("agent name 不得為空");
        }
        WalletInfo w = createWallet();
        agents.put(name, w.address().toLowerCase());
        persist();
        return w;
    }

    /** 全部 agent 名→{address, did}。 */
    public Map<String, Object> listAgents() {
        Map<String, Object> out = new LinkedHashMap<>();
        agents.forEach((name, addr) -> out.put(name, Map.of(
                "address", addr, "did", "did:potato:" + addr)));
        return out;
    }

    /** DID/地址/name → 錢包資訊（含 agent 名,若已註冊）。 */
    public Map<String, Object> agentInfo(String didOrName) {
        String addr = resolveAddressText(didOrName);
        KeyPair kp = wallets.get(addr.toLowerCase());
        if (kp == null && serviceWallet != null
                && serviceWallet.address().equalsIgnoreCase(addr)) {
            kp = serviceWallet;
        }
        String name = null;
        for (var e : agents.entrySet()) {
            if (e.getValue().equalsIgnoreCase(addr)) name = e.getKey();
        }
        var m = new LinkedHashMap<String, Object>();
        m.put("address", addr.toLowerCase());
        m.put("did", "did:potato:" + addr.toLowerCase());
        m.put("name", name != null ? name : "");
        m.put("managed", kp != null);
        if (kp != null) m.put("publicKey", kp.publicKeyHex());
        return m;
    }

    /** personal_sign——address 可為 0x 地址、did:potato: 或 "service"。 */
    public Map<String, Object> sign(String address, String message) {
        KeyPair kp = resolve(address);
        byte[] hash = EthereumCrypto.personalMessageHash(
                message.getBytes(StandardCharsets.UTF_8));
        Signature sig = EthereumCrypto.sign(kp.privateKey(), hash);
        return Map.of(
                "address", kp.address(),
                "message", message,
                "r", "0x" + EthereumCrypto.Hex.pad64(sig.r()),
                "s", "0x" + EthereumCrypto.Hex.pad64(sig.s()),
                "v", 27 + sig.recId(),
                "signature", sig.toHex65());
    }

    /** ecrecover 驗章——signature 為 65B hex（r‖s‖v,v=27/28 或 0/1）。 */
    public Map<String, Object> verify(String address, String message,
                                      String signature) {
        byte[] b = EthereumCrypto.Hex.decode(signature);
        if (b.length != 65) {
            throw new IllegalArgumentException("signature 需為 65 bytes hex");
        }
        int v = b[64] & 0xFF;
        int recId = v >= 27 ? v - 27 : v;
        byte[] hash = EthereumCrypto.personalMessageHash(
                message.getBytes(StandardCharsets.UTF_8));
        String recovered = EthereumCrypto.recoverAddress(hash,
                new Signature(new BigInteger(1, b, 0, 32),
                              new BigInteger(1, b, 32, 32), recId));
        boolean ok = recovered != null
                && recovered.equalsIgnoreCase(address);
        return Map.of(
                "valid", ok,
                "recovered", recovered != null ? recovered : "",
                "expected", address);
    }

    // ---- 內部 ----

    /** "did:potato:0x.." / "0x.." / agent name → 0x 地址字串。 */
    private String resolveAddressText(String id) {
        if (id == null) return "";
        String s = id;
        if (s.startsWith("did:potato:")) {
            s = s.substring("did:potato:".length());
        }
        if (s.startsWith("0x") && s.length() == 42) return s;
        String named = agents.get(s);
        return named != null ? named : s;
    }

    private KeyPair resolve(String address) {
        if (address == null || address.isBlank() || "service".equals(address)) {
            return serviceWallet;
        }
        String addr = resolveAddressText(address);
        KeyPair kp = wallets.get(addr.toLowerCase());
        if (kp == null) {
            throw new IllegalArgumentException(
                    "錢包不存在（本服務只持有 create/import/agent 進來的 key）: "
                    + address);
        }
        return kp;
    }

    private WalletInfo info(KeyPair kp) {
        return new WalletInfo(kp.address(),
                "did:potato:" + kp.address(), kp.publicKeyHex());
    }

    private static String strip0x(String hex) {
        return hex.startsWith("0x") ? hex.substring(2) : hex;
    }
}
