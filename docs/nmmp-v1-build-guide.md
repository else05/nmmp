# NMMP V1 构建与验证说明

本文用于构建并验证 NMMP DIY V1。以下命令以 Windows PowerShell 为例。

## 1. 计算并行任务数

构建使用逻辑处理器数量减 3，避免占满系统：

```powershell
$jobs = [Math]::Max(1, [Environment]::ProcessorCount - 3)
Write-Host "逻辑处理器数: $([Environment]::ProcessorCount)，构建并行度: $jobs"
```

当前 8 个逻辑处理器的机器会使用 5 路并行。

## 2. 构建保护工具

```powershell
cd E:\OtherProject\safe-toolchain\nmmp\nmm-protect

.\gradlew.bat :arsc:build --max-workers=$jobs --console=plain
.\gradlew.bat build --max-workers=$jobs --console=plain --stacktrace
```

构建成功后，可执行的 fat JAR 位于：

```text
E:\OtherProject\safe-toolchain\nmmp\nmm-protect\build\libs\
```

## 3. 单独验证 VM/native

根据本机 SDK 路径调整以下变量：

```powershell
cd E:\OtherProject\safe-toolchain\nmmp\nmmvm

$env:ANDROID_HOME = 'D:\Android\SDK'
$env:CMAKE_BUILD_PARALLEL_LEVEL = "$jobs"

.\gradlew.bat :nmmvm:assembleDebug `
    --max-workers=$jobs `
    --console=plain `
    --stacktrace
```

`--max-workers` 限制 Gradle 并行度，`CMAKE_BUILD_PARALLEL_LEVEL` 限制 CMake 构建并行度。

该步骤验证独立 VM 工程能够编译，但不会覆盖实际保护流程的生产静态链接验证。

## 4. 验证生产保护路径

使用新构建的 JAR 处理一个较小的 APK。这一步会生成 C 源码，并编译 VM 静态链接版 `libnmmp.so`，是 V1 最重要的集成验证。

```powershell
$env:ANDROID_HOME = 'D:\Android\SDK'
$env:ANDROID_SDK_HOME = 'D:\Android\SDK'
$env:ANDROID_NDK_HOME = 'D:\Android\SDK\ndk\27.0.12077973'
$env:CMAKE_PATH = 'D:\Android\SDK\cmake\3.22.1'
$env:CMAKE_BUILD_PARALLEL_LEVEL = "$jobs"

cd E:\OtherProject\safe-toolchain\nmmp\nmm-protect\build\libs

$jar = Get-ChildItem .\vm-protect-*.jar |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

 java -jar $jar.FullName apk D:\AndroidProjects\sync-ui\app\build\outputs\apk\release\MdoHelper_v1.4.0M_202607282311_release.apk  D:\AndroidProjects\sync-ui\app\convertRules.txt D:\AndroidProjects\sync-ui\app\build\outputs\mapping\release\mapping.txt
```

如果没有 mapping 文件，请按原有 NMMP 命令参数执行。处理结果默认位于输入 APK 目录下的 `build` 目录。

生产路径至少应确认：

- CMake 配置和 5 路 native 编译成功。
- 最终只打包 `libnmmp.so`，不再单独打包 `libnmmvm.so`。
- 生成的 JNI 方法使用 `vmEncodedCode` 和 `vmExecute`。
- 生成的 Resolver 包含编码字符串池及一次性解码逻辑。
- APK 能正常安装，并覆盖普通调用、异常处理和多线程调用。

## 5. 运行关键测试

```powershell
cd E:\OtherProject\safe-toolchain\nmmp\nmm-protect

.\gradlew.bat :apkprotect:test `
    --max-workers=$jobs `
    --console=plain `
    --stacktrace
```

只运行 codec 黄金向量测试：

```powershell
.\gradlew.bat :apkprotect:test `
    --tests com.nmmedit.apkprotect.dex2c.MethodCodecTest `
    --max-workers=$jobs `
    --console=plain
```

## 6. 常见问题

### VM 模板版本不匹配

如果提示 `VM 模板版本不匹配`，说明 JAR 旁边的 `tools\vmsrc.zip` 是旧缓存。

删除该旧缓存文件后重新执行，让新版 JAR 释放当前模板。V1 会故意拒绝不匹配的外部模板，不会静默覆盖。

### 没有持续构建日志

使用 `--console=plain`。需要更详细日志时追加 `--info`。

### 修改 VM 源码后重新生成内置模板

仓库中的模板由以下脚本生成：

```text
nmm-protect\mksrc\build-src.sh
```

执行后应确认以下文件已进入 `vmsrc.zip`：

- `vm/Codec.cpp`
- `vm/VmCodec.cpp`
- `vm/include/VmCodec.h`
- `vm/include/VmCodecConfig.h`
- 更新后的生产 `CMakeLists.txt`

