# NMMP O-MVLL 第一批核心保护实施方案

> 状态：代码与真实 arm64 构建验证完成；设备安装和长时间回归待执行  
> 日期：2026-07-30  
> 范围：保护 NMMP 解释器入口、运行时解码算法、字符串池解码和 Resolver 核心函数。  
> 明确不包含：opcode 映射表、Resolver 全局映射数组等静态映射数据的编码改造。

## 1. 目标

在不改变现有 DEX 指令语义、随机 opcode 映射协议和 Resolver 数据结构的前提下，使用 O-MVLL 提高以下真实运行时代码的静态分析和直接 hook 成本：

1. `vmInterpret` 解释器主体。
2. `vmExecute` 指令与异常表解码入口。
3. `vmCodecKeyByte` 密钥流算法。
4. `vmCodecTransform` 缓冲区解码算法。
5. `decodeStringPool` 字符串池解码入口。
6. `dvmResolveField`、`dvmResolveMethod`、`dvmResolveClass`、`dvmFindClass`、`dvmConstantString` 等 Resolver 核心函数。
7. 能直接暴露上述核心位置的 C/C++ 诊断字符串。

本方案只保护最终 APK 构建目录 `dex2c` 中的真实 native 模块。`scripts/wsl/smoke` 只用于确认插件能够加载，不作为 NMMP 核心保护的验收依据。

## 2. 明确不做

本轮不实施以下内容：

- 不编码或重构 `handlerTable`。
- 不增加二级 opcode 映射。
- 不编码 `gClassIds`、`gFieldIds`、`gMethodIds`、`gStringIds`、`gTypeIds`、`gSignatureIds` 等 Resolver 全局映射数组。
- 不修改现有随机 opcode 生成协议。
- 不对全部生成的 native wrapper 启用 O-MVLL。
- 不对整个 `vmInterpret` 启用控制流平坦化、全量算术混淆、间接分支、函数拆分或高比例基本块复制。
- 不对 `JNIWrapper.c`、`GlobalCache.cpp`、`Exception.cpp` 进行重度保护。

## 3. 实施前置：集中化 codec

当前 `VmCodec.h` 中的 `static inline` 实现会被复制到多个翻译单元，导致只保护一个模块时仍可能存在未保护算法副本。

调整为：

```text
VmCodec.cpp
  ├── 内部 vmCodecKeyByte
  ├── vmCodecTransform
  └── vmCodecHash

VmCodec.h
  └── 只保留跨 C/C++ 调用所需的函数声明
```

约束：

- `vmCodecKeyByte` 保持当前翻译单元内部可见，并允许编译器内联到 `vmCodecTransform`。
- 生成的 C 文件通过 `extern "C"` 兼容声明调用 `vmCodecTransform` 和 `vmCodecHash`。
- 调用粒度保持为整段缓冲区，不引入每字节一次跨函数调用。
- `Codec.cpp` 和生成的字符串池解码代码必须使用同一份实现。

## 4. O-MVLL 保护组合

| 目标 | BreakControlFlow | AntiHook | OpaqueConstants | Arithmetic | FlattenCFG | StringEncoding |
| --- | --- | --- | --- | --- | --- | --- |
| `vmInterpret` | 开启 | 开启 | `> 255` | 关闭 | 关闭 | 仅核心诊断字符串 |
| `vmExecute` | 开启 | 开启 | `> 255` | 关闭 | 关闭 | 开启 |
| `vmCodecKeyByte` | 关闭 | 关闭 | `> 255` | 2 轮 | 关闭 | 关闭 |
| `vmCodecTransform` | 关闭 | 开启 | `> 255` | 关闭 | 关闭 | 关闭 |
| `decodeStringPool` | 关闭 | 关闭 | `> 255` | 关闭 | 开启 | 开启 |
| Resolver 核心函数 | 关闭 | 关闭 | `> 3` | 关闭 | 开启 | 开启 |

以下能力保持关闭：

- `IndirectCall`
- `IndirectBranch`
- `BasicBlockDuplicate`
- `FunctionOutline`
- 全局 `shuffle_functions`
- 全局 `inline_jni_wrappers`

## 5. 真实模块白名单

生产配置只匹配真实 APK 构建路径：

```text
/dex2c/vm/InterpC-portable.cpp
/dex2c/vm/VmCodec.cpp
/dex2c/vm/Codec.cpp
/dex2c/generated/classes*_native_functions.c
```

必须排除：

```text
/scripts/wsl/smoke/
```

生成文件只按函数名启用 pass，禁止对整个 `classes*_native_functions.c` 进行全量混淆。

## 6. 冒烟验证

冒烟源码使用独立文件名和独立函数名，避免与真实 runtime 的 `Codec.cpp`、`vmExecute`、`vmCodecKeyByte` 混淆。

冒烟验证只确认：

- NDK Clang 能加载 O-MVLL 插件。
- Python 配置可以被插件读取。
- 至少一个测试函数能产生预期 pass 日志。

冒烟成功不能代表真实 NMMP 代码已经受到保护。

## 7. 实施顺序

### 阶段 1：集中化 codec

1. 新增 `VmCodec.cpp`。
2. 将 `VmCodec.h` 改为声明文件。
3. 更新独立 VM 和生产模板的 CMake 源清单。
4. 运行 codec 固定向量测试，确认 Java 编码端和 C++ 解码端仍然配套。

### 阶段 2：启用第一批 O-MVLL 保护

1. 使用真实 `dex2c` 路径匹配生产模块。
2. 对 `vmInterpret` 启用 BreakControlFlow、AntiHook 和有限常量保护。
3. 对 `vmExecute` 启用 BreakControlFlow、AntiHook、常量保护和核心字符串保护。
4. 对 `vmCodecKeyByte` 启用两轮算术混淆和常量保护。
5. 对 `vmCodecTransform` 启用 AntiHook 和常量保护。
6. 对 `decodeStringPool` 启用 FlattenCFG 和常量保护。
7. 对 Resolver 核心函数启用 FlattenCFG、常量保护和核心字符串保护。

### 阶段 3：构建与验证

1. 重建 `vmsrc.zip`。
2. 重建 `vm-protect-*.jar`。
3. 运行 codec 和 native 构建相关测试。
4. 使用完整 APK 流程构建 arm64-v8a SO。
5. 检查真实构建目录中的 O-MVLL 模块日志。
6. 安装并回归真实 APK。

## 8. 验收标准

真实 APK 构建日志至少应确认：

```text
InterpC-portable.cpp
  vmInterpret
  Changes applied

VmCodec.cpp
  vmCodecKeyByte
  vmCodecTransform
  Changes applied

Codec.cpp
  vmExecute
  Changes applied

classes*_native_functions.c
  decodeStringPool
  dvmResolveField
  dvmResolveMethod
  dvmResolveClass
  dvmFindClass
  dvmConstantString
  Changes applied
```

功能回归至少覆盖：

- 普通、静态和实例方法。
- 字段和方法 Resolver。
- 字符串常量。
- switch、try/catch、数组初始化。
- 协程、递归和多线程调用。
- Android 8.1 长时间运行稳定性。

同时记录保护前后的：

- SO 大小。
- native 构建时间。
- APK 首次加载时间。
- 受保护方法执行时间。

任何目标模块只有出现实际 `Changes applied` 才算保护生效；仅出现 pass 的 `Executing` 不算完成。

## 9. 2026-07-30 执行结果

已完成：

- codec 实现已集中到 `VmCodec.cpp`，生成代码与 VM 共用同一套转换和 hash 实现。
- `vmsrc.zip`、`vm-protect-*.jar` 已重建；打包脚本会强制用 `mksrc` CMake 模板覆盖同名条目，不再依赖文件时间戳。
- `BuildNativeLibTest` 2/2、`MethodCodecTest` 2/2 通过。
- O-MVLL 独立冒烟验证通过。
- O-MVLL 处理后的 codec 固定向量测试已在 Android 8.1 arm64 设备通过，返回码为 0。
- 使用真实规则和 mapping 完成 arm64-v8a APK 构建。

真实 O-MVLL 日志确认：

- `vmInterpret`：AntiHook、StringEncoding、BreakControlFlow 已应用。
- `vmExecute`：AntiHook、StringEncoding、BreakControlFlow 已应用。
- `vmCodecKeyByte` / `vmCodecTransform` 所在模块：OpaqueConstants、Arithmetic、AntiHook 已应用。
- `decodeStringPool` 与 Resolver 核心函数：ControlFlowFlattening、OpaqueConstants 已应用。
- IndirectCall、IndirectBranch、BasicBlockDuplicate 等未启用项均未应用。

`vmInterpret` 和 `vmExecute` 没有符合 `> 255` 条件的常量，因此 OpaqueConstants 在这两个模块没有产生修改；生成的解码和 Resolver 函数没有可处理的直接字符串字面量，因此 StringEncoding 没有产生修改。这两项属于白名单内无合格目标，不影响其他已确认生效的 pass。

本次没有增加 opcode 表、`handlerTable` 或 Resolver 全局映射数组的编码，也没有修改其数据协议。设备安装、Android 8.1 长时间运行、性能和体积对比仍需在签名 APK 与目标设备上完成。
