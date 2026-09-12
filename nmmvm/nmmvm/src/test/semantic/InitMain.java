package com.nmmedit.semantic;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class InitMain {
    private static native void configure(int mode);
    private static native boolean activate();
    private static native boolean requireReady();
    private static native void releaseBinding();
    private static native int bindingCalls();
    private static final CountDownLatch callbackEntered = new CountDownLatch(1);
    private static final CountDownLatch callbackRelease = new CountDownLatch(1);
    private static final AtomicInteger callbacks = new AtomicInteger();
    public static void registrationCallback() throws Exception {
        if (!activate() || !requireReady()) throw new AssertionError("registration owner reentry");
        callbacks.incrementAndGet(); callbackEntered.countDown();
        if (!callbackRelease.await(10, TimeUnit.SECONDS)) throw new AssertionError("callback timeout");
    }
    public static void earlyCallback() {
        if (requireReady()) throw new AssertionError("early business call accepted");
    }
    public static void main(String[] args) throws Exception {
        System.load(args[0]); int mode = Integer.parseInt(args[1]); configure(mode);
        CountDownLatch entered = new CountDownLatch(16);
        AtomicInteger returned = new AtomicInteger(), succeeded = new AtomicInteger();
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread[] threads = new Thread[16];
        for (int i = 0; i < threads.length; ++i) {
            threads[i] = new Thread(() -> {
                entered.countDown();
                try { if (activate()) succeeded.incrementAndGet(); }
                catch (Throwable t) { failure.compareAndSet(null, t); }
                finally { returned.incrementAndGet(); }
            });
            threads[i].start();
        }
        if (!entered.await(10, TimeUnit.SECONDS)) throw new AssertionError("caller timeout");
        releaseBinding();
        if (mode != 2) {
            if (!callbackEntered.await(10, TimeUnit.SECONDS)) throw new AssertionError("missing JNI callback");
            if (returned.get() != 0) throw new AssertionError("activation returned before registration completed");
            callbackRelease.countDown();
        }
        for (Thread thread : threads) { thread.join(10000); if (thread.isAlive()) throw new AssertionError("waiter deadlock"); }
        if (failure.get() != null) throw new AssertionError(failure.get());
        if (bindingCalls() != 1 || returned.get() != 16 || succeeded.get() != (mode == 0 ? 16 : 0)
                || callbacks.get() != (mode == 2 ? 0 : 1)) throw new AssertionError("inconsistent publication");
        if (mode != 0 && activate()) throw new AssertionError("failed initialization retried");
        System.out.println("INIT_JNI_PASS mode=" + mode + " callers=16 callbacks=" + callbacks.get());
    }
}
