import java.io.DataInputStream;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** Build-only JDK 17+ AEAD helper. Secret inputs use stdin, never argv/files. */
class Seal {
    public static void main(String[] args) throws Exception {
        DataInputStream input = new DataInputStream(System.in);
        byte[] key = new byte[32];
        byte[] header = new byte[80];
        input.readFully(key);
        input.readFully(header);
        byte[] plain = input.readNBytes(64 * 1024 * 1024 + 1);
        if (plain.length > 64 * 1024 * 1024) throw new IllegalArgumentException("payload limit");
        try {
            Cipher cipher = Cipher.getInstance("ChaCha20-Poly1305");
            cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "ChaCha20"),
                    new IvParameterSpec(Arrays.copyOfRange(header, 64, 76)));
            cipher.updateAAD(header);
            System.out.write(cipher.doFinal(plain));
        } finally {
            Arrays.fill(key, (byte) 0);
            Arrays.fill(plain, (byte) 0);
        }
    }
}
