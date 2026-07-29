package com.nmmedit.apkprotect.dex2c.converter;


import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.MethodImplementation;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.android.tools.smali.util.Hex;

import javax.annotation.Nonnull;
import java.util.Iterator;
import java.util.List;

public class MyMethodUtil {
    public static boolean isConstructorOrAbstract(Method method) {
        String name = method.getName();
        if (name.equals("<init>") || name.equals("<clinit>")) {//构造方法或静态构造方法
            return true;
        }
        int accessFlags = method.getAccessFlags();
        //本地方法或者抽象方法
        return AccessFlags.NATIVE.isSet(accessFlags)
                || AccessFlags.ABSTRACT.isSet(accessFlags);
    }

    public static boolean isBridgeOrSynthetic(Method method) {
        int flags = method.getAccessFlags();
        return AccessFlags.BRIDGE.isSet(flags) ||
                AccessFlags.SYNTHETIC.isSet(flags);
    }

    public static boolean isEmptyVoidMethod(Method method) {
        if (!"V".equals(method.getReturnType())) {
            return false;
        }
        final MethodImplementation implementation = method.getImplementation();
        if (implementation == null || !implementation.getTryBlocks().isEmpty()) {
            return false;
        }

        // 只接受唯一一条 return-void，避免忽略编译器插入的参数校验、父类调用等逻辑
        final Iterator<? extends Instruction> iterator = implementation.getInstructions().iterator();
        if (!iterator.hasNext() || iterator.next().getOpcode() != Opcode.RETURN_VOID) {
            return false;
        }
        return !iterator.hasNext();
    }

    @Nonnull
    public static String getMethodSignature(List<? extends CharSequence> parameterTypes, String returnType) {
        StringBuilder sig = new StringBuilder();
        sig.append("(");
        for (CharSequence parameterType : parameterTypes) {
            sig.append(parameterType);
        }
        sig.append(")");
        sig.append(returnType);
        return sig.toString();
    }

    //jni函数命名规则
    @Nonnull
    public static String getJniFunctionName(String className, String methodName,
                                            List<? extends CharSequence> parameterTypes, String returnType) {

        StringBuilder funcName = new StringBuilder("Java_");

        funcName.append(nameReplace(className).replace('/', '_'));

        funcName.append('_');
        funcName.append(nameReplace(methodName));
        if (!parameterTypes.isEmpty()) {
            funcName.append("__");
            for (CharSequence parameterType : parameterTypes) {
                funcName.append(nameReplace(parameterType.toString()).replace('/', '_'));
            }
        }
        //todo 有的dex混淆后可能导致类名,方法名和参数完全一样,只有返回类型不一样,
        // 这样使用标准的jni命名规则导致同名函数产生,需要加上返回类型以区分函数,同时必须注册native方法
        if (returnType != null) {
            funcName.append('_').append(nameReplace(returnType).replace('/', '_'));
        }
        return funcName.toString();
    }

    private static String nameReplace(String s) {
        int length = s.length();
        StringBuilder sb = new StringBuilder(length * 6);
        for (int i = 0; i < length; i++) {
            char c = s.charAt(i);
            switch (c) {
                case '_':
                    sb.append("_1");
                    break;
                case ';':
                    sb.append("_2");
                    break;
                case '[':
                    sb.append("_3");
                    break;
                case '$':
                case '-':
                case '+':
                    sb.append("_0");
                    sb.append(Hex.u2(c));
                    break;
                default:
                    if (
                            ((c & 0xFFFF) > 0x7F)
                    ) {
                        // 非ascii字符
                        sb.append("_0");
                        sb.append(Hex.u2(c));
                    } else {
                        sb.append(c);
                    }
                    break;
            }
        }
        return sb.toString();
    }
}