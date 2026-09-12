package bench;

/** Native declarations match the separately compiled implementation DEX. */
public final class BenchBody {
    public static native int arithmetic(int n);
    public static native int branch(int n);
    public static native int shortCall(int x);
    public static native int largeShort(int x);
    public static native int jniObject(String value);
    public static native int arrayPayload(int index);
    public static native int exception(int divisor);
    public static native int recursive(int depth);
}
