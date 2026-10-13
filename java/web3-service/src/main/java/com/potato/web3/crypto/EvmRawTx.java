package com.potato.web3.crypto;

import java.io.ByteArrayOutputStream;
import java.math.BigInteger;
import java.util.List;

import org.bouncycastle.util.BigIntegers;

/**
 * EVM legacy 交易的 RLP 編碼與 EIP-155 簽章——
 * eth_sendRawTransaction 用的 0x raw tx。
 */
public final class EvmRawTx {

    private EvmRawTx() {
    }

    /**
     * 簽 legacy 交易（EIP-155）。
     * @return signed raw tx bytes；調用端 hex 化後走 eth_sendRawTransaction
     */
    public static byte[] sign(BigInteger nonce, BigInteger gasPrice,
                              BigInteger gasLimit, byte[] to,
                              BigInteger value, byte[] data,
                              BigInteger chainId, BigInteger privKey) {
        // unsigned：尾部塞 chainId,0,0
        byte[] unsigned = rlpList(List.of(
                rlpUint(nonce), rlpUint(gasPrice), rlpUint(gasLimit),
                rlpBytes(to), rlpUint(value), rlpBytes(data),
                rlpUint(chainId), rlpBytes(new byte[0]),
                rlpBytes(new byte[0])));
        byte[] hash = EthereumCrypto.keccak256(unsigned);

        EthereumCrypto.Signature sig = EthereumCrypto.sign(privKey, hash);
        BigInteger v = chainId.multiply(BigInteger.TWO)
                .add(BigInteger.valueOf(35 + sig.recId()));

        return rlpList(List.of(
                rlpUint(nonce), rlpUint(gasPrice), rlpUint(gasLimit),
                rlpBytes(to), rlpUint(value), rlpBytes(data),
                rlpUint(v),
                rlpBytes(BigIntegers.asUnsignedByteArray(sig.r())),
                rlpBytes(BigIntegers.asUnsignedByteArray(sig.s()))));
    }

    // ---- RLP ----

    static byte[] rlpUint(BigInteger v) {
        return rlpBytes(v.signum() == 0 ? new byte[0]
                : BigIntegers.asUnsignedByteArray(v));
    }

    static byte[] rlpBytes(byte[] b) {
        if (b.length == 1 && (b[0] & 0xFF) < 0x80) {
            return b;
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeLen(out, b.length, 0x80);
        out.write(b, 0, b.length);
        return out.toByteArray();
    }

    static byte[] rlpList(List<byte[]> items) {
        ByteArrayOutputStream payload = new ByteArrayOutputStream();
        for (byte[] item : items) {
            payload.write(item, 0, item.length);
        }
        byte[] p = payload.toByteArray();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeLen(out, p.length, 0xC0);
        out.write(p, 0, p.length);
        return out.toByteArray();
    }

    private static void writeLen(ByteArrayOutputStream out, int len,
                                 int offset) {
        if (len <= 55) {
            out.write(offset + len);
        } else {
            byte[] lenBytes = BigIntegers.asUnsignedByteArray(
                    BigInteger.valueOf(len));
            out.write(offset + 55 + lenBytes.length);
            out.write(lenBytes, 0, lenBytes.length);
        }
    }
}
