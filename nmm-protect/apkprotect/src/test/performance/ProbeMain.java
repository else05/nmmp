package bench;

import com.google.libc.ReactNative;
import java.util.Arrays;

public final class ProbeMain {
    public static void main(String[] args) {
        System.load(args[0]);
        ReactNative.js(0);
        BenchMain.verify();
        int iterations = Integer.parseInt(args[2]);
        BenchProbe.reset();
        long checksum = BenchMain.run(args[1], iterations);
        long[] counters = BenchProbe.snapshot();
        if (checksum != BenchMain.expected(args[1], iterations)) throw new AssertionError("probe checksum mismatch");
        System.out.println("{\"case\":\"" + args[1] + "\",\"iterations\":" + iterations +
                ",\"checksum\":" + checksum + ",\"counters\":" + Arrays.toString(counters) + "}");
    }
}
