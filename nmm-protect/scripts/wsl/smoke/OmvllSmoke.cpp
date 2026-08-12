static int omvllSmokeArithmetic(int value) {
    unsigned int mixed = static_cast<unsigned int>(value) ^ 0x13579bdfU;
    mixed = (mixed << 7U) | (mixed >> 25U);
    return static_cast<int>(mixed + 0x2468ace0U);
}

extern "C" int omvllSmokeEntry(int value) {
    int result = omvllSmokeArithmetic(value);
    if ((result & 1) == 0) {
        result ^= 0x55aa55aa;
    } else {
        result += 0x1020304;
    }
    for (int i = 0; i < 4; ++i) {
        result = (result << 3) ^ (result >> 2) ^ i;
    }
    return result;
}
