package com.nmmedit.apkprotect.dex2c.converter.structs;

import com.android.tools.smali.dexlib2.AccessFlags;
import com.android.tools.smali.dexlib2.HiddenApiRestriction;
import com.android.tools.smali.dexlib2.Opcode;
import com.android.tools.smali.dexlib2.base.reference.BaseMethodReference;
import com.android.tools.smali.dexlib2.base.reference.BaseTypeReference;
import com.android.tools.smali.dexlib2.builder.MutableMethodImplementation;
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction10x;
import com.android.tools.smali.dexlib2.builder.instruction.BuilderInstruction3rc;
import com.android.tools.smali.dexlib2.iface.*;
import com.android.tools.smali.dexlib2.immutable.reference.ImmutableMethodReference;
import com.google.common.collect.Iterables;

import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

public class ApplicationInitClassDef extends BaseTypeReference implements ClassDef {
    private static final String ATTACH_BASE_CONTEXT = "attachBaseContext";
    private final ClassDef classDef;
    private final String type;
    private final String superclass;
    private final String initClass;
    private final String initMethod;

    public ApplicationInitClassDef(ClassDef classDef, String initClass, String initMethod) {
        this.classDef = classDef;
        this.type = classDef.getType();
        this.superclass = classDef.getSuperclass();
        this.initClass = initClass;
        this.initMethod = initMethod;
    }

    public ApplicationInitClassDef(String type, String initClass, String initMethod) {
        this.classDef = null;
        this.type = type;
        this.superclass = "Landroid/app/Application;";
        this.initClass = initClass;
        this.initMethod = initMethod;
    }

    @Nonnull
    @Override
    public String getType() {
        return type;
    }

    @Override
    public int getAccessFlags() {
        return classDef == null ? AccessFlags.PUBLIC.getValue() : classDef.getAccessFlags();
    }

    @Nullable
    @Override
    public String getSuperclass() {
        return superclass;
    }

    @Nonnull
    @Override
    public List<String> getInterfaces() {
        return classDef == null ? Collections.emptyList() : classDef.getInterfaces();
    }

    @Nullable
    @Override
    public String getSourceFile() {
        return classDef == null ? null : classDef.getSourceFile();
    }

    @Nonnull
    @Override
    public Set<? extends Annotation> getAnnotations() {
        return classDef == null ? Collections.emptySet() : classDef.getAnnotations();
    }

    @Nonnull
    @Override
    public Iterable<? extends Field> getStaticFields() {
        return classDef == null ? Collections.emptyList() : classDef.getStaticFields();
    }

    @Nonnull
    @Override
    public Iterable<? extends Field> getInstanceFields() {
        return classDef == null ? Collections.emptyList() : classDef.getInstanceFields();
    }

    @Nonnull
    @Override
    public Iterable<? extends Field> getFields() {
        return Iterables.concat(getStaticFields(), getInstanceFields());
    }

    @Nonnull
    @Override
    public Iterable<? extends Method> getDirectMethods() {
        if (classDef != null) return classDef.getDirectMethods();
        return Collections.singletonList(new EmptyConstructorMethod(type, superclass));
    }

    @Nonnull
    @Override
    public Iterable<? extends Method> getVirtualMethods() {
        final List<Method> methods = new ArrayList<>();
        boolean found = false;
        if (classDef != null) {
            for (Method method : classDef.getVirtualMethods()) {
                if (isAttachBaseContext(method)) {
                    methods.add(new AttachBaseContextMethod(method));
                    found = true;
                } else {
                    methods.add(method);
                }
            }
        }
        if (!found) methods.add(new AttachBaseContextMethod(null));
        return methods;
    }

    @Nonnull
    @Override
    public Iterable<? extends Method> getMethods() {
        return Iterables.concat(getDirectMethods(), getVirtualMethods());
    }

    private static boolean isAttachBaseContext(Method method) {
        return ATTACH_BASE_CONTEXT.equals(method.getName())
                && method.getParameterTypes().equals(
                Collections.singletonList(RegisterNativesUtilClassDef.CONTEXT_TYPE))
                && "V".equals(method.getReturnType());
    }

    private class AttachBaseContextMethod extends BaseMethodReference implements Method {
        private final Method method;

        AttachBaseContextMethod(Method method) {
            this.method = method;
        }

        @Nonnull
        @Override
        public String getDefiningClass() {
            return type;
        }

        @Nonnull
        @Override
        public String getName() {
            return ATTACH_BASE_CONTEXT;
        }

        @Nonnull
        @Override
        public List<? extends CharSequence> getParameterTypes() {
            return Collections.singletonList(RegisterNativesUtilClassDef.CONTEXT_TYPE);
        }

        @Nonnull
        @Override
        public List<? extends MethodParameter> getParameters() {
            return method == null ? Collections.emptyList() : method.getParameters();
        }

        @Nonnull
        @Override
        public String getReturnType() {
            return "V";
        }

        @Override
        public int getAccessFlags() {
            return method == null ? AccessFlags.PROTECTED.getValue() : method.getAccessFlags();
        }

        @Nonnull
        @Override
        public Set<? extends Annotation> getAnnotations() {
            return method == null ? Collections.emptySet() : method.getAnnotations();
        }

        @Nonnull
        @Override
        public Set<HiddenApiRestriction> getHiddenApiRestrictions() {
            return method == null ? Collections.emptySet() : method.getHiddenApiRestrictions();
        }

        @Override
        public MethodImplementation getImplementation() {
            if (method == null || method.getImplementation() == null) {
                return createImplementation();
            }
            final MutableMethodImplementation implementation =
                    new MutableMethodImplementation(method.getImplementation());
            final int contextRegister = implementation.getRegisterCount() - 1;
            implementation.addInstruction(0, buildInitCall(contextRegister));
            return implementation;
        }

        private MethodImplementation createImplementation() {
            final MutableMethodImplementation implementation =
                    new MutableMethodImplementation(2);
            implementation.addInstruction(buildInitCall(1));
            implementation.addInstruction(new BuilderInstruction3rc(
                    Opcode.INVOKE_SUPER_RANGE,
                    0,
                    2,
                    new ImmutableMethodReference(
                            superclass,
                            ATTACH_BASE_CONTEXT,
                            Collections.singletonList(RegisterNativesUtilClassDef.CONTEXT_TYPE),
                            "V")));
            implementation.addInstruction(new BuilderInstruction10x(Opcode.RETURN_VOID));
            return implementation;
        }

        private BuilderInstruction3rc buildInitCall(int contextRegister) {
            return new BuilderInstruction3rc(
                    Opcode.INVOKE_STATIC_RANGE,
                    contextRegister,
                    1,
                    new ImmutableMethodReference(
                            initClass,
                            initMethod,
                            Collections.singletonList(RegisterNativesUtilClassDef.CONTEXT_TYPE),
                            "V"));
        }
    }
}
