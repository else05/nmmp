import java.util.concurrent.atomic.AtomicInteger;

public final class InvariantMain {
    private static native long eval(int scenario, boolean checked, int capacity);
    private static final AtomicInteger checks = new AtomicInteger();
    private static void eq(long expected, long actual) {
        if (actual != expected) throw new AssertionError(actual + " != " + expected);
        checks.incrementAndGet();
    }
    private static void reject(int scenario, int capacity) {
        try { eval(scenario, true, capacity); }
        catch (InternalError expected) { checks.incrementAndGet(); return; }
        throw new AssertionError("Missing register/reference rejection");
    }
    public static int callback() {
        // JNI -> Java -> JNI: inner calls use different modes/capacities from the outer frame.
        return (int)(eval(0, false, 0) + eval(0, true, 2));
    }
    public static int sum6(int a, int b, int c, int d, int e, int f) { return a+b+c+d+e+f; }
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        for (int i = 0; i < 32; ++i) {
            eq(12, eval(0, false, 0));
            eq(12, eval(6, false, 0));
            eq(12, eval(0, true, 2));
            reject(0, 0); reject(0, 1);
            eq(0x0123456789abcdefL, eval(1, true, 8)); reject(1, 7);
            reject(2, 8); reject(3, 8); reject(5, 8);
            eq(27, eval(4, false, 0)); eq(27, eval(4, true, 2)); reject(4, 1);
        }
        Thread[] threads = new Thread[4];
        Throwable[] failures = new Throwable[4];
        for (int t = 0; t < threads.length; ++t) {
            final int index = t;
            threads[t] = new Thread(() -> {
                try {
                    for (int i = 0; i < 100; ++i) {
                        eq(12, eval(0, false, 0)); eq(12, eval(0, true, 2));
                        reject(0, 1);
                    }
                } catch (Throwable error) { failures[index] = error; }
            });
            threads[t].start();
        }
        for (Thread thread : threads) thread.join();
        for (Throwable failure : failures) if (failure != null) throw new AssertionError(failure);
        System.out.println("INVARIANT_PASS checks=" + checks.get());
    }
}
