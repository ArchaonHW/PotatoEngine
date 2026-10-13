package com.potato.web3.crypto;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;

import org.junit.jupiter.api.Test;

import com.potato.web3.crypto.EthereumCrypto.KeyPair;
import com.potato.web3.crypto.EthereumCrypto.Signature;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * secp256k1/keccak256/personal_sign 已知向量與簽章-還原 roundtrip。
 */
class EthereumCryptoTest {

    @Test
    void keccak256EmptyVector() {
        // keccak-256("")——Ethereum 全程用 Keccak 變體（非 SHA3-256）
        assertEquals(
            "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
            EthereumCrypto.Hex.encode(EthereumCrypto.keccak256(new byte[0])));
    }

    @Test
    void privateKeyOneAddress() {
        // privkey=0x01 的公開地址是眾所周知的 dev 向量
        KeyPair kp = EthereumCrypto.fromPrivateKey(BigInteger.ONE);
        assertEquals("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf",
                kp.address());
    }

    @Test
    void signRecoverRoundtrip() {
        KeyPair kp = EthereumCrypto.generateKeyPair();
        byte[] hash = EthereumCrypto.personalMessageHash(
                "potato test".getBytes(StandardCharsets.UTF_8));
        Signature sig = EthereumCrypto.sign(kp.privateKey(), hash);
        String recovered = EthereumCrypto.recoverAddress(hash, sig);
        assertNotNull(recovered);
        assertEquals(kp.address(), recovered);
    }

    @Test
    void signatureIs65BytesHex() {
        KeyPair kp = EthereumCrypto.generateKeyPair();
        byte[] hash = EthereumCrypto.keccak256(new byte[]{1, 2, 3});
        Signature sig = EthereumCrypto.sign(kp.privateKey(), hash);
        byte[] raw = EthereumCrypto.Hex.decode(sig.toHex65());
        assertEquals(65, raw.length);
        // r ‖ s ‖ v
        assertArrayEquals(BigIntegersTo32(sig.r()), slice(raw, 0, 32));
        assertArrayEquals(BigIntegersTo32(sig.s()), slice(raw, 32, 32));
        int v = raw[64] & 0xFF;
        assertTrue(v == 0 || v == 1 || v == 27 || v == 28);
    }

    @Test
    void personalHashPrefix() {
        // "\x19Ethereum Signed Message:\n<len><msg>"
        byte[] msg = "hi".getBytes(StandardCharsets.UTF_8);
        byte[] h = EthereumCrypto.personalMessageHash(msg);
        assertEquals(32, h.length);
        // 相同輸入 → 相同 hash（keccak 為確定性雜湊）
        assertArrayEquals(h, EthereumCrypto.personalMessageHash(msg));
    }

    private static byte[] BigIntegersTo32(BigInteger v) {
        byte[] b = v.toByteArray();
        byte[] out = new byte[32];
        int src = Math.max(0, b.length - 32);
        int len = Math.min(32, b.length);
        System.arraycopy(b, b.length - len, out, 32 - len, len);
        return out;
    }

    private static byte[] slice(byte[] a, int off, int len) {
        byte[] out = new byte[len];
        System.arraycopy(a, off, out, 0, len);
        return out;
    }
}
