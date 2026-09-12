package com.nmmedit.semantic;

public final class SemanticMain {
    private static native long eval(int scenario, long input, Object object);
    private static int checks;
    private static void eq(long expected, long actual) {
        if (expected != actual) throw new AssertionError(expected + " != " + actual);
        ++checks;
    }
    private static void failure(Class<? extends Throwable> type, int scenario, Object object) {
        try { eval(scenario, 0, object); }
        catch (Throwable ex) {
            if (!type.isInstance(ex)) throw new AssertionError(ex);
            ++checks; return;
        }
        throw new AssertionError("expected " + type.getName());
    }
    public static void main(String[] args) {
        System.load(args[0]);
        for (int n : new int[]{-9, 0, 1, 2, 17, 2000}) {
            eq(n + 7, eval(0, n, null));
            long sum = 0; for (int i = 0; i < n; ++i) sum += i;
            eq(sum, eval(1, n, null));
        }
        for (long n : new long[]{0, -1, 0x0123456789abcdefL, Long.MIN_VALUE, Long.MAX_VALUE})
            eq(n ^ 0x0123456789abcdefL, eval(2, n, null));
        for (int n : new int[]{Integer.MIN_VALUE, -1, 0, 1, 2, 3, 4, Integer.MAX_VALUE})
            eq(n == 1 ? 11 : n == 2 ? 22 : 33, eval(3, n, null));
        for (int n : new int[]{Integer.MIN_VALUE, -10000, 0, 10000, Integer.MAX_VALUE})
            eq(n == -10000 ? 11 : n == 10000 ? 22 : 33, eval(4, n, null));
        for (int width : new int[]{1, 2, 4, 8}) {
            Object array = width == 1 ? new byte[259] : width == 2 ? new short[259]
                    : width == 4 ? new int[259] : new long[259];
            eq(259, eval(5, width, array));
            for (int i = 0; i < 259; ++i) {
                long value = width == 1 ? ((byte[])array)[i] : width == 2 ? ((short[])array)[i]
                        : width == 4 ? ((int[])array)[i] : ((long[])array)[i];
                eq(width == 1 ? (byte)i : i, value);
            }
        }
        eq(77, eval(6, 0, null)); // catch-all after a real ART null throw
        failure(NullPointerException.class, 7, null);
        StringBuilder sb = new StringBuilder("reader-jni");
        eq(10, eval(8, 0, sb)); // invoke-object / move-result-object / second invoke
        eq(41, eval(9, 0, sb)); // unused object result must be discarded
        failure(NullPointerException.class, 8, null);
        if (args.length > 1 && args[1].equals("reader")) {
            failure(InternalError.class, 10, null);
            failure(InternalError.class, 11, null);
            byte[] unchanged = new byte[259];
            try { eval(12, 8, unchanged); throw new AssertionError("array width accepted"); }
            catch (InternalError expected) { ++checks; }
            for (byte value : unchanged) eq(0, value);
            try { eval(12, 8, new Object[259]); throw new AssertionError("object array accepted"); }
            catch (InternalError expected) { ++checks; }
            if (args.length > 2 && args[2].equals("demand"))
                for (int scenario = 20; scenario <= 32; ++scenario) failure(InternalError.class, scenario, null);
        }
        System.out.println("SEMANTIC_PASS checks=" + checks);
    }
}
