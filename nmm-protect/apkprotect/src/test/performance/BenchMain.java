package bench;

import com.google.libc.ReactNative;
import java.io.BufferedReader;
import java.io.FileReader;

public final class BenchMain {
    static volatile long sink;

    static long run(String name, int iterations) {
        long sum = 0;
        switch (name) {
            case "arithmetic": for (int i = 0; i < iterations; ++i) sum += BenchBody.arithmetic(200); break;
            case "branch": for (int i = 0; i < iterations; ++i) sum += BenchBody.branch(200); break;
            case "shortCall": for (int i = 0; i < iterations; ++i) sum += BenchBody.shortCall(i & 255); break;
            case "largeShort": for (int i = 0; i < iterations; ++i) sum += BenchBody.largeShort(0); break;
            case "jniObject": for (int i = 0; i < iterations; ++i) sum += BenchBody.jniObject("nmmp-object-check"); break;
            case "arrayPayload": for (int i = 0; i < iterations; ++i) sum += BenchBody.arrayPayload(i & 127); break;
            case "exception": for (int i = 0; i < iterations; ++i) sum += BenchBody.exception(0); break;
            case "recursive": for (int i = 0; i < iterations; ++i) sum += BenchBody.recursive(8); break;
            case "multiThread": sum = concurrent(iterations); break;
            default: throw new IllegalArgumentException(name);
        }
        sink = sum;
        return sum;
    }

    static long concurrent(final int iterations) {
        final long[] sums = new long[4];
        final Throwable[] failures = new Throwable[4];
        final java.util.concurrent.CountDownLatch ready = new java.util.concurrent.CountDownLatch(4);
        final java.util.concurrent.CountDownLatch start = new java.util.concurrent.CountDownLatch(1);
        Thread[] threads = new Thread[4];
        try {
        for (int t = 0; t < 4; ++t) {
            final int index = t;
            threads[t] = new Thread(() -> {
                ready.countDown();
                try {
                    start.await();
                    long sum = 0;
                    for (int i = 0; i < iterations; ++i) sum += BenchBody.shortCall(i & 255);
                    sums[index] = sum;
                } catch (Throwable failure) { failures[index] = failure; }
            });
            threads[t].start();
        }
            ready.await();
            start.countDown();
            for (Thread thread : threads) thread.join();
        } catch (InterruptedException failure) { throw new AssertionError(failure); }
        finally {
            start.countDown();
            boolean interrupted = false;
            for (Thread thread : threads) {
                if (thread == null) continue;
                while (thread.isAlive()) {
                    try { thread.join(); } catch (InterruptedException failure) { interrupted = true; }
                }
            }
            if (interrupted) Thread.currentThread().interrupt();
        }
        for (Throwable failure : failures) if (failure != null) throw new AssertionError(failure);
        return sums[0] + sums[1] + sums[2] + sums[3];
    }

    static long expected(String name, int iterations) {
        long sum = 0;
        if (name.equals("shortCall") || name.equals("multiThread")) {
            long block = 0;
            for (int i = 0; i < 256; ++i) block += (i * 3) ^ (i >>> 2);
            sum = block * (iterations / 256);
            for (int i = 0; i < iterations % 256; ++i) sum += (i * 3) ^ (i >>> 2);
            return name.equals("multiThread") ? 4 * sum : sum;
        }
        if (name.equals("arrayPayload")) {
            long block = 0;
            for (int i = 0; i < 128; ++i) block += i * 17 + 3;
            sum = block * (iterations / 128);
            for (int i = 0; i < iterations % 128; ++i) sum += i * 17 + 3;
            return sum;
        }
        int value;
        switch (name) {
            case "arithmetic": value = 17; for (int i = 0; i < 200; ++i) value = (value * 33 + i) ^ (value >>> 7); break;
            case "branch": value = 0; for (int i = 0; i < 200; ++i) { if ((i & 3) == 0) value += i * 7; else value ^= i + 13; } break;
            case "largeShort": value = 7; break;
            case "jniObject": value = 126; break;
            case "exception": value = 9; break;
            case "recursive": value = 37; break;
            default: throw new IllegalArgumentException(name);
        }
        return (long)value * iterations;
    }

    static void verify() {
        int arithmetic = 17, branch = 0;
        for (int i = 0; i < 200; ++i) {
            arithmetic = (arithmetic * 33 + i) ^ (arithmetic >>> 7);
            if ((i & 3) == 0) branch += i * 7; else branch ^= i + 13;
        }
        if (BenchBody.arithmetic(200) != arithmetic || BenchBody.branch(200) != branch ||
                BenchBody.largeShort(0) != 7 || BenchBody.jniObject("nmmp-object-check") != 126)
            throw new AssertionError("result mismatch");
        for (int i = 0; i < 256; ++i)
            if (BenchBody.shortCall(i) != ((i * 3) ^ (i >>> 2))) throw new AssertionError("short result");
        for (int i = 0; i < 128; ++i)
            if (BenchBody.arrayPayload(i) != i * 17 + 3) throw new AssertionError("array payload result");
        if (BenchBody.exception(0) != 9 || BenchBody.exception(4) != 25 || BenchBody.recursive(8) != 37)
            throw new AssertionError("exception/recursive result");
    }

    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        ReactNative.js(0);
        verify();
        String name = args[1];
        int iterations = Integer.parseInt(args[2]), warmups = Integer.parseInt(args[3]), rounds = Integer.parseInt(args[4]);
        long expected = expected(name, iterations);
        for (int i = -warmups; i < rounds; ++i) {
            long start = System.nanoTime(), checksum = run(name, iterations), elapsed = System.nanoTime() - start;
            if (checksum != expected) throw new AssertionError("timed checksum mismatch: " + name);
            System.out.println("{\"case\":\"" + name + "\",\"round\":" + i + ",\"iterations\":" + iterations +
                    ",\"elapsedNs\":" + elapsed + ",\"checksum\":" + checksum + "}");
        }
        try (BufferedReader reader = new BufferedReader(new FileReader("/proc/self/status"))) {
            String line;
            while ((line = reader.readLine()) != null)
                if (line.startsWith("VmHWM:") || line.startsWith("VmRSS:")) System.out.println(line);
        }
    }
}
