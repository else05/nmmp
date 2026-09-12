import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class ColdPublicationMain {
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        Class<?> bridge = Class.forName("com.nmmedit.semantic.DecodedWipeMain");
        Method eval = bridge.getDeclaredMethod("eval", int.class, Throwable.class);
        Method snapshot = bridge.getDeclaredMethod("snapshot");
        eval.setAccessible(true);
        snapshot.setAccessible(true);
        CountDownLatch ready = new CountDownLatch(16), go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        Thread[] threads = new Thread[16];
        for (int i = 0; i < threads.length; ++i) {
            threads[i] = new Thread(() -> {
                ready.countDown();
                try {
                    go.await();
                    for (int j = 0; j < 100; ++j)
                        if (((Integer) eval.invoke(null, 4, null)) != 27) throw new AssertionError();
                } catch (Throwable ex) { failure.compareAndSet(null, ex); }
            });
            threads[i].start();
        }
        ready.await();
        go.countDown();
        for (Thread thread : threads) thread.join();
        if (failure.get() != null) throw new AssertionError(failure.get());
        long[] n = (long[]) snapshot.invoke(null);
        if (n[0] != 0 || n[1] != 4800 || n[2] != n[1] || n[3] != n[1] || n[4] != n[1] ||
                n[5] != 0 || n[6] != 0 || n[8] != 3200 || n[10] != 0) throw new AssertionError(Arrays.toString(n));
        System.out.println("COLD_PUBLICATION_PASS calls=1600 threads=16 counters=" + Arrays.toString(n));
    }
}
