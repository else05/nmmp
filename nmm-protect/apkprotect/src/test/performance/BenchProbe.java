package bench;

/** Present only in the separately instrumented allocation/coverage build. */
public final class BenchProbe {
    public static native void reset();
    public static native long[] snapshot();
}
