package com.potato.web3.crypto;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Arrays;

import org.bouncycastle.asn1.sec.SECNamedCurves;
import org.bouncycastle.asn1.x9.X9ECParameters;
import org.bouncycastle.crypto.digests.KeccakDigest;
import org.bouncycastle.crypto.digests.SHA256Digest;
import org.bouncycastle.crypto.generators.ECKeyPairGenerator;
import org.bouncycastle.crypto.params.ECDomainParameters;
import org.bouncycastle.crypto.params.ECKeyGenerationParameters;
import org.bouncycastle.crypto.params.ECPrivateKeyParameters;
import org.bouncycastle.crypto.params.ECPublicKeyParameters;
import org.bouncycastle.crypto.signers.ECDSASigner;
import org.bouncycastle.crypto.signers.HMacDSAKCalculator;
import org.bouncycastle.math.ec.ECPoint;
import org.bouncycastle.util.BigIntegers;

/**
 * Ethereum 加密原語（Bouncy Castle 低階 API,免 JCA provider 註冊）：
 *   keccak256 / secp256k1 金鑰 / deterministic-k ECDSA 簽章 /
 *   low-s 正規化 + recId / ecrecover / 地址推導。
 */
public final class EthereumCrypto {

    public static final X9ECParameters CURVE =
            SECNamedCurves.getByName("secp256k1");
    public static final ECDomainParameters DOMAIN = new ECDomainParameters(
            CURVE.getCurve(), CURVE.getG(), CURVE.getN(), CURVE.getH());
    private static final BigInteger HALF_N =
            CURVE.getN().shiftRight(1);

    private static final SecureRandom RNG = new SecureRandom();

    /** (r, s, recId)——recId 0..3,供 ecrecover 用。 */
    public record Signature(BigInteger r, BigInteger s, int recId) {
        public String toHex65() {
            return "0x" + Hex.pad64(r) + Hex.pad64(s)
                    + String.format("%02x", recId);
        }
    }

    public record KeyPair(BigInteger privateKey, ECPoint publicKey) {
        public String privateKeyHex() {
            return Hex.pad64(privateKey);
        }

        /** uncompressed 公鑰 64 bytes（x‖y，去 0x04 前綴）。 */
        public byte[] publicKeyBytes() {
            byte[] enc = publicKey.getEncoded(false);
            return Arrays.copyOfRange(enc, 1, enc.length);
        }

        public String publicKeyHex() {
            return Hex.encode(publicKeyBytes());
        }

        /** Ethereum 地址 = keccak256(pubkey) 末 20 bytes。 */
        public String address() {
            byte[] hash = keccak256(publicKeyBytes());
            return "0x" + Hex.encode(
                    Arrays.copyOfRange(hash, 12, hash.length));
        }
    }

    private EthereumCrypto() {
    }

    public static byte[] keccak256(byte[] input) {
        KeccakDigest d = new KeccakDigest(256);
        d.update(input, 0, input.length);
        byte[] out = new byte[32];
        d.doFinal(out, 0);
        return out;
    }

    public static KeyPair generateKeyPair() {
        ECKeyPairGenerator gen = new ECKeyPairGenerator();
        gen.init(new ECKeyGenerationParameters(DOMAIN, RNG));
        var kp = gen.generateKeyPair();
        return new KeyPair(
                ((ECPrivateKeyParameters) kp.getPrivate()).getD(),
                ((ECPublicKeyParameters) kp.getPublic()).getQ());
    }

    public static KeyPair fromPrivateKey(BigInteger d) {
        return new KeyPair(d, DOMAIN.getG().multiply(d).normalize());
    }

    /** Ethereum personal_sign：hash = keccak("\x19Ethereum Signed Message:\n"+len+msg)。 */
    public static byte[] personalMessageHash(byte[] message) {
        byte[] prefix = ("\u0019Ethereum Signed Message:\n"
                + message.length).getBytes(StandardCharsets.US_ASCII);
        byte[] joined = new byte[prefix.length + message.length];
        System.arraycopy(prefix, 0, joined, 0, prefix.length);
        System.arraycopy(message, 0, joined, prefix.length, message.length);
        return keccak256(joined);
    }

    /** deterministic-k（RFC6979）簽 hash；回傳含 recId 的可恢復簽章。 */
    public static Signature sign(BigInteger privKey, byte[] hash) {
        ECDSASigner signer = new ECDSASigner(
                new HMacDSAKCalculator(new SHA256Digest()));
        signer.init(true, new ECPrivateKeyParameters(privKey, DOMAIN));
        BigInteger[] rs = signer.generateSignature(hash);
        BigInteger r = rs[0];
        BigInteger s = rs[1];
        BigInteger e = new BigInteger(1, hash);

        // low-s 正規化；recId 對應 R 點 y 的奇偶
        int recId = -1;
        ECPoint pub = DOMAIN.getG().multiply(privKey).normalize();
        for (int i = 0; i < 4; i++) {
            ECPoint q = recover(e, r, s, i);
            if (q != null && q.equals(pub)) {
                recId = i;
                break;
            }
        }
        if (recId < 0) {
            throw new IllegalStateException("ecrecover 失敗");
        }
        if (s.compareTo(HALF_N) > 0) {
            s = CURVE.getN().subtract(s);
            recId ^= 1;  // s 翻轉 → R 點取另一側 y
        }
        return new Signature(r, s, recId);
    }

    /** ecrecover：給 hash/(r,s)/recId 還原簽署者公鑰。 */
    public static ECPoint recover(BigInteger e, BigInteger r, BigInteger s,
                                  int recId) {
        BigInteger n = CURVE.getN();
        // x = r + j*n（recId bit1 表示 x 超過曲線階一次）
        BigInteger x = r.add(BigInteger.valueOf(recId / 2).multiply(n));
        if (x.compareTo(CURVE.getCurve().getField().getCharacteristic()) >= 0) {
            return null;
        }
        ECPoint R;
        try {
            R = decompressKey(x, (recId & 1) == 1);
        } catch (IllegalArgumentException ex) {
            return null;
        }
        if (!R.multiply(n).isInfinity()) {
            return null;  // R 不在子群——secp256k1 cofactor=1 理論上恆成立
        }
        BigInteger rInv = r.modInverse(n);
        // Q = r^-1 (sR - eG)
        ECPoint sR = R.multiply(s);
        ECPoint eG = DOMAIN.getG().multiply(e.mod(n));
        return sR.subtract(eG).multiply(rInv).normalize();
    }

    /** 由簽章還原地址（verify 用——不需持有私鑰）。 */
    public static String recoverAddress(byte[] hash, Signature sig) {
        BigInteger e = new BigInteger(1, hash);
        ECPoint q = recover(e, sig.r(), sig.s(), sig.recId());
        if (q == null) {
            return null;
        }
        byte[] enc = q.getEncoded(false);
        byte[] pub = Arrays.copyOfRange(enc, 1, enc.length);
        byte[] addrHash = keccak256(pub);
        return "0x" + Hex.encode(
                Arrays.copyOfRange(addrHash, 12, addrHash.length));
    }

    private static ECPoint decompressKey(BigInteger x, boolean odd) {
        byte[] enc = new byte[33];
        enc[0] = (byte) (odd ? 0x03 : 0x02);
        System.arraycopy(BigIntegers.asUnsignedByteArray(32, x),
                0, enc, 1, 32);
        return CURVE.getCurve().decodePoint(enc);
    }

    /** hex 工具（inner class 避免再引 codec 依賴）。 */
    public static final class Hex {
        private Hex() {
        }

        public static String encode(byte[] b) {
            StringBuilder sb = new StringBuilder(b.length * 2);
            for (byte v : b) {
                sb.append(String.format("%02x", v & 0xFF));
            }
            return sb.toString();
        }

        public static byte[] decode(String hex) {
            String h = hex.startsWith("0x") ? hex.substring(2) : hex;
            byte[] out = new byte[h.length() / 2];
            for (int i = 0; i < out.length; i++) {
                out[i] = (byte) Integer.parseInt(
                        h.substring(i * 2, i * 2 + 2), 16);
            }
            return out;
        }

        /** BigInteger → 64 hex（32B 靠左補零）。 */
        public static String pad64(BigInteger v) {
            return String.format("%064x", v);
        }
    }
}
