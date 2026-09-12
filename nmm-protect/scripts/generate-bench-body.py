"""Emit the fixed input implementation; timer and native declarations are separate."""
from pathlib import Path
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
cases = '\n'.join('            case %d: return x * %d + %d;' % (i, i * 13 + 7, i * 97)
                  for i in range(1, 513))
(out / 'BenchBody.java').write_text('''package bench;
public final class BenchBody {
    public static int arithmetic(int n) {
        int x = 17;
        for (int i = 0; i < n; ++i) x = (x * 33 + i) ^ (x >>> 7);
        return x;
    }
    public static int branch(int n) {
        int x = 0;
        for (int i = 0; i < n; ++i) {
            if ((i & 3) == 0) x += i * 7; else x ^= i + 13;
        }
        return x;
    }
    public static int shortCall(int x) { return (x * 3) ^ (x >>> 2); }
    public static int largeShort(int x) {
        if (x == 0) return 7;
        switch (x) {
''' + cases + '''
            default: return -1;
        }
    }
    public static int jniObject(String value) { return value.substring(1).length() + value.charAt(0); }
    public static int arrayPayload(int index) {
        int[] values = new int[]{''' + ','.join(str(i * 17 + 3) for i in range(128)) + '''};
        return values[index];
    }
    public static int exception(int divisor) {
        try { return 100 / divisor; } catch (ArithmeticException failure) { return 9; }
    }
    public static int recursive(int depth) {
        if (depth <= 0) return 1;
        return recursive(depth - 1) + depth;
    }
}
''', encoding='utf-8')
