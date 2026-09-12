package com.nmmedit.semantic;

import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class DecodedWipeMain {
    private static native int eval(int scenario, Throwable pending);
    private static native long[] snapshot();
    private static void require(boolean ok) { if (!ok) throw new AssertionError(); }
    public static int callback() { return eval(0, null) + eval(0, null); }
    private static void run() {
        require(eval(0, null) == 12);
        try { eval(1, null); throw new AssertionError(); }
        catch (NullPointerException expected) { }
        try { eval(2, null); throw new AssertionError(); }
        catch (InternalError expected) { }
        require(eval(3, null) == 77);
        require(eval(4, null) == 27);
        IllegalStateException original = new IllegalStateException("preserve identity");
        try { eval(5, original); throw new AssertionError(); }
        catch (IllegalStateException actual) { require(actual == original); }
    }
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        long[] start = snapshot();
        run();
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread[] threads = new Thread[8];
        for (int i = 0; i < threads.length; ++i) {
            threads[i] = new Thread(() -> {
                try { go.await(); for (int j = 0; j < 100; ++j) run(); }
                catch (Throwable ex) { failure.compareAndSet(null, ex); }
            });
            threads[i].start();
        }
        go.countDown();
        for (Thread thread : threads) thread.join();
        if (failure.get() != null) throw new AssertionError(failure.get());
        long[] end = snapshot();
        for (int i = 0; i < end.length; ++i) end[i] -= start[i];
        // Each run: 8 entries (6 direct, 2 nested), 5 normal incl caught, 1 uncaught, 2 read failures.
        long runs = 801;
        require(end[0] == 0 && end[1] == runs * 8 && end[2] == end[1] && end[3] == end[1]);
        require(end[4] == runs * 5 && end[5] == runs && end[6] == runs * 2);
        require(end[7] > runs * 8 && end[8] == runs * 2 && end[9] > 0 && end[10] == 0);
        System.out.println("DECODE_WIPE_PASS runs=" + runs + " counters=" + Arrays.toString(end));
    }
}
