from functools import lru_cache
from pathlib import Path

import omvll


class NmmpConfig(omvll.ObfuscationConfig):
    RESOLVER_FUNCTIONS = {
        "dvmResolveField",
        "dvmResolveMethod",
        "dvmResolveClass",
        "dvmFindClass",
        "dvmConstantString",
    }

    @staticmethod
    def module_path(module: omvll.Module) -> str:
        return (module.name or "").replace("\\", "/")

    @classmethod
    def is_real_vm_module(cls, module: omvll.Module, filename: str) -> bool:
        path = cls.module_path(module)
        return "/dex2c/" in path and path.endswith("/vm/" + filename)

    @classmethod
    def is_generated_module(cls, module: omvll.Module) -> bool:
        path = cls.module_path(module)
        filename = path.rsplit("/", 1)[-1]
        return (
            "/dex2c/generated/" in path
            and filename.startswith("classes")
            and filename.endswith("_native_functions.c")
        )

    @classmethod
    def is_smoke_module(cls, module: omvll.Module) -> bool:
        return cls.module_path(module).endswith("/scripts/wsl/smoke/OmvllSmoke.cpp")

    @staticmethod
    def is_function(function: omvll.Function, name: str) -> bool:
        raw_name = function.name or ""
        demangled_name = function.demangled_name or ""
        return (
            raw_name == name
            or raw_name.startswith(name + ".__omvll_body")
            or demangled_name == name
            or demangled_name.startswith(name + "(")
        )

    @classmethod
    def is_interpreter(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_real_vm_module(module, "InterpC-portable.cpp")
            and cls.is_function(function, "vmInterpret")
        )

    @classmethod
    def is_vm_execute(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_real_vm_module(module, "Codec.cpp")
            and cls.is_function(function, "vmExecute")
        )

    @classmethod
    def is_codec_key(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_real_vm_module(module, "VmCodec.cpp")
            and cls.is_function(function, "vmCodecKeyByte")
        )

    @classmethod
    def is_codec_transform(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_real_vm_module(module, "VmCodec.cpp")
            and cls.is_function(function, "vmCodecTransform")
        )

    @classmethod
    def is_string_decoder(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_generated_module(module)
            and cls.is_function(function, "decodeStringPool")
        )

    @classmethod
    def is_resolver_function(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return cls.is_generated_module(module) and any(
            cls.is_function(function, name)
            for name in cls.RESOLVER_FUNCTIONS
        )

    @classmethod
    def is_smoke_entry(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_smoke_module(module)
            and cls.is_function(function, "omvllSmokeEntry")
        )

    @classmethod
    def is_smoke_arithmetic(cls, module: omvll.Module, function: omvll.Function) -> bool:
        return (
            cls.is_smoke_module(module)
            and cls.is_function(function, "omvllSmokeArithmetic")
        )

    def break_control_flow(self, module: omvll.Module, function: omvll.Function):
        return (
            self.is_interpreter(module, function)
            or self.is_vm_execute(module, function)
            or self.is_smoke_entry(module, function)
        )

    def anti_hooking(self, module: omvll.Module, function: omvll.Function):
        return (
            self.is_interpreter(module, function)
            or self.is_vm_execute(module, function)
            or self.is_codec_transform(module, function)
        )

    def flatten_cfg(self, module: omvll.Module, function: omvll.Function):
        return (
            self.is_string_decoder(module, function)
            or self.is_resolver_function(module, function)
        )

    def obfuscate_constants(self, module: omvll.Module, function: omvll.Function):
        if self.is_resolver_function(module, function):
            return omvll.OpaqueConstantsLowerLimit(3)

        if (
            self.is_interpreter(module, function)
            or self.is_vm_execute(module, function)
            or self.is_codec_key(module, function)
            or self.is_codec_transform(module, function)
            or self.is_string_decoder(module, function)
            or self.is_smoke_entry(module, function)
            or self.is_smoke_arithmetic(module, function)
        ):
            return omvll.OpaqueConstantsLowerLimit(255)

        return False

    def obfuscate_arithmetic(self, module: omvll.Module, function: omvll.Function):
        if (
            self.is_codec_key(module, function)
            or self.is_smoke_arithmetic(module, function)
        ):
            return omvll.ArithmeticOpt(2)

        return False

    def obfuscate_string(
            self,
            module: omvll.Module,
            function: omvll.Function,
            _string: bytes):
        if (
            self.is_interpreter(module, function)
            or self.is_vm_execute(module, function)
            or self.is_string_decoder(module, function)
            or self.is_resolver_function(module, function)
        ):
            return omvll.StringEncOptGlobal()

        return False


@lru_cache(maxsize=1)
def omvll_get_config() -> omvll.ObfuscationConfig:
    # 禁止默认全局变化，只允许上面的真实 runtime 白名单和独立冒烟目标生效。
    omvll.config.shuffle_functions = False
    omvll.config.inline_jni_wrappers = False
    omvll.config.output_folder = str(Path(__file__).resolve().parent / "logs")
    return NmmpConfig()
