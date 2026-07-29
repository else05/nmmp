package com.nmmedit.apkprotect.dex2c.filters;

import com.android.tools.smali.dexlib2.iface.Method;

public interface MethodConversionReporter {
    void onMethodConverted(Method method);

    void printReport();
}
