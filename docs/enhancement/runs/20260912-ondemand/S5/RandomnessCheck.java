import com.nmmedit.apkprotect.dex2c.GeneratorRandom;
import java.security.SecureRandom;

public final class RandomnessCheck {
    public static void main(String[] args) {
        boolean absent = GeneratorRandom.configuredTestSeed() == null;
        boolean secure = GeneratorRandom.create("production-environment-check") instanceof SecureRandom;
        if (!absent || !secure) throw new IllegalStateException("Test randomness configured in production environment");
        System.out.println("PRODUCTION_ENVIRONMENT_CHECK testSeedAbsent=true secureRandom=true");
    }
}
