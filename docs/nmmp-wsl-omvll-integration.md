# NMMP WSL + O-MVLL 整合方案

## 1. 目标与边界

本方案保留原来的 Windows 处理方式，同时增加一条独立的 WSL + O-MVLL 处理通道：

```text
nmmp.cmd
  → Windows Java
  → Windows NDK r27
  → 不加载 O-MVLL

nmmp-wsl.sh
  → Kali WSL Java
  → Linux NDK r26d
  → 加载 O-MVLL
```

约束：

- `E:\env-tool\nmmp.cmd` 不修改。
- 两条通道使用同一个 `vm-protect-*.jar`。
- 只有设置 `OMVLL_PLUGIN` 时才启用插件。
- O-MVLL 通道仅支持 `arm64-v8a` 和 `armeabi-v7a`，遇到其他 ABI 直接终止，避免静默输出未保护的 SO。
- 第一批保护范围以 [nmmp-omvll-first-batch-plan.md](./nmmp-omvll-first-batch-plan.md) 为准：覆盖解释器、集中后的 codec、字符串池解码和 Resolver 核心函数；不保护 `JNIWrapper.c`，也不全量保护生成 wrapper。

环境安装说明见 [nmmp-wsl-omvll-environment.md](./nmmp-wsl-omvll-environment.md)。

## 2. 文件布局

项目内保留可追踪模板：

```text
nmm-protect/scripts/wsl/nmmp-env.sh
nmm-protect/scripts/wsl/nmmp-wsl.sh
nmm-protect/scripts/wsl/omvll-config.py
nmm-protect/scripts/wsl/verify-omvll.sh
nmm-protect/scripts/wsl/smoke/OmvllSmoke.cpp
```

实际部署位置：

```text
D:\Android\SDK_WSL\nmmp-env.sh
D:\Android\SDK_WSL\omvll\1.8.0\config.py
E:\env-tool\nmmp-wsl.sh
```

O-MVLL 和 Python 标准库位置：

```text
D:\Android\SDK_WSL\omvll\1.8.0\omvll_ndk_r26d.so
D:\Android\SDK_WSL\omvll\1.8.0\Python-3.10.7\Lib
```

## 3. Java 接入

`BuildNativeLib` 的行为：

1. 优先读取 `ANDROID_HOME`，兼容旧的 `ANDROID_SDK_HOME`。
2. 读取 `OMVLL_PLUGIN`。
3. `OMVLL_PLUGIN` 为空时不增加任何插件参数，Windows 流程保持原样。
4. `OMVLL_PLUGIN` 非空时校验文件存在，并向 CMake 传入：

```text
-DNMMP_OMVLL_PLUGIN=/mnt/d/Android/SDK_WSL/omvll/1.8.0/omvll_ndk_r26d.so
```

5. CMake、Ninja 和 Clang 直接继承 Java 进程的以下环境变量：

```text
OMVLL_CONFIG
OMVLL_PYTHONPATH
LD_LIBRARY_PATH
CMAKE_BUILD_PARALLEL_LEVEL
```

6. 子进程 stdout 和 stderr 合并读取，避免 O-MVLL 输出较多时两个管道互相阻塞。

## 4. CMake 接入

顶层 `mksrc/CMakeLists.txt` 定义可选缓存变量：

```cmake
set(NMMP_OMVLL_PLUGIN "" CACHE FILEPATH "O-MVLL plugin path")
```

只有插件路径非空且 ABI 为 ARM 时，才向最终共享库目标添加：

```text
-fpass-plugin=<omvll_ndk_r26d.so>
```

`mksrc/vm/CMakeLists.txt` 同时向 `nmmvm` 静态库添加插件。必须在静态库源文件编译阶段加载插件；只在最终 `libc++_en.so` 链接阶段设置不会处理 `Codec.cpp`。

## 5. 第一批保护范围

`config.py` 同时按真实 `dex2c` 模块路径和函数名建立白名单。C++ 内部函数同时检查 `name` 与 `demangled_name`，避免依赖固定的 LLVM 修饰名。

| 目标 | 已配置保护 |
| --- | --- |
| `vmInterpret` | BreakControlFlow、AntiHook、有限 Opaque Constants、核心 String Encoding |
| `vmExecute` | BreakControlFlow、AntiHook、有限 Opaque Constants、String Encoding |
| `vmCodecKeyByte` | Opaque Constants、Arithmetic 2 rounds |
| `vmCodecTransform` | AntiHook、Opaque Constants |
| `decodeStringPool` | Control-Flow Flattening、Opaque Constants、String Encoding |
| Resolver 核心函数 | Control-Flow Flattening、Opaque Constants、String Encoding |

Resolver 白名单包含：

```text
dvmResolveField
dvmResolveMethod
dvmResolveClass
dvmFindClass
dvmConstantString
```

全局关闭：

```text
IndirectCall
IndirectBranch
BasicBlockDuplicate
FunctionOutline
shuffle_functions
inline_jni_wrappers
```

不对整个 `vmInterpret` 启用 Control-Flow Flattening，也不保护静态 opcode/Resolver 映射数据，避免解释器热路径出现明显性能下降或引入新的编解码协议。

## 6. 重新生成 native 模板

修改 `mksrc` 后，在 WSL 中执行：

```bash
cd /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/mksrc
bash ./build-src.sh
```

该命令更新：

```text
nmm-protect/apkprotect/src/main/resources/vmsrc.zip
```

然后继续使用原来的 Windows Gradle 方式构建 JAR：

```powershell
cd E:\OtherProject\safe-toolchain\nmmp\nmm-protect
$cores = [Environment]::ProcessorCount
$workers = [Math]::Max(1, $cores - 3)
.\gradlew.bat jar --max-workers=$workers
```

重新构建后需要删除一次旧缓存：

```text
nmm-protect/build/libs/tools/vmsrc.zip
```

运行新 JAR 时会从 JAR 内重新释放最新模板。旧缓存只检查模板版本，不会检查 CMake 内容，因此不删除会导致 O-MVLL CMake 配置不生效。

## 7. WSL 使用方式

进入 Kali WSL：

```powershell
wsl.exe -d kali
```

进入 APK 所在目录：

```bash
cd /mnt/d/AndroidProjects/sync-ui/app/build/outputs/apk/release
```

执行（`E:\env-tool` 已进入 WSL 继承的 `PATH`）：

```bash
nmmp-wsl.sh \
  ./MdoHelper_v1.4.0M_release.apk \
  -r /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt \
  -m /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt
```

参数与 `nmmp.cmd` 一致：

```text
nmmp-wsl <apk> [-r <rules>] [-m <mapping>]
```

不带参数显示帮助。相对路径和 Java 工作目录均以调用脚本时的目录为准。未指定 `-r` 时，如果调用目录存在 `convertRules.txt`，会自动使用。

输出格式：

```text
<调用目录>/<原文件名>_nmmp年月日时分秒.apk
```

WSL 脚本只接受 `/mnt/d/...`、`/mnt/e/...` 等 Linux 路径，不转换 `D:\...` Windows 路径。

## 8. 验证

### Windows 原通道

```powershell
nmmp.cmd app-release.apk -r convertRules.txt -m mapping.txt
```

日志应显示：

```text
[nmmp] O-MVLL: disabled
```

编译命令中不应出现 `-fpass-plugin`。

### WSL O-MVLL 通道

先验证 NDK Clang 能加载插件并执行白名单配置：

```bash
bash /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/verify-omvll.sh
```

成功时输出：

```text
[nmmp-wsl] O-MVLL whitelist passed
```

再处理 APK：

```bash
nmmp-wsl.sh \
  app-release.apk  -r /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt -m /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt

  
  /mnt/d/AndroidProjects/sync-ui/app/build/outputs/apk/release
  
 -r /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt -m /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt
```

日志应显示插件路径，并在 CMake 配置阶段出现：

```text
NMMP O-MVLL enabled
```

生成 `compile_commands.json` 后可以检查：

```bash
grep -n -- '-fpass-plugin=' \
  build/.cxx/cmake/Release/arm64-v8a/compile_commands.json
```

至少应在 `InterpC-portable.cpp`、`Codec.cpp`、`VmCodec.cpp` 和生成的 `classes*_native_functions.c` 编译命令中出现插件参数。

最终还需验证：

- APK 能安装。
- Android 8.1 上启动正常。
- 实际调用受保护方法正常。
- 长时间运行不闪退。
- 对比启用前后的 SO 体积和关键方法耗时。

## 9. 回退

不需要回退代码即可停用 O-MVLL：

- 使用原来的 `nmmp.cmd`；或
- 在 WSL 环境中取消 `OMVLL_PLUGIN` 后使用普通流程。

原 `nmmp.cmd`、Windows NDK r27、DEX 编码规则、opcode 映射和 `MethodCodec` 均未改变；`vmInterpret` 的源代码语义不变，仅在 WSL 产物中应用 O-MVLL。
