# NMMP WSL + O-MVLL 环境配置

## 1. 目标

在 Kali WSL 中使用 Linux 版 NDK、CMake 和 O-MVLL 编译 NMMP 的 native 模块，同时将体积较大的工具链保存在 Windows 的 D 盘，避免明显增大 WSL 虚拟磁盘。

Windows 保存路径：

```text
D:\Android\SDK_WSL
```

Kali WSL 中对应路径：

```text
/mnt/d/Android/SDK_WSL
```

注意：

- `D:\Android\SDK_WSL` 中必须安装 Linux 版 Android 工具，不能直接复用 `D:\Android\SDK` 中的 Windows 版 NDK、CMake。
- WSL 中运行的 Clang、CMake 和 O-MVLL 都必须是 Linux x86_64 文件。
- 当前推荐组合为 NDK r26d、CMake 3.22.1、O-MVLL 1.8.0。

## 2. 推荐目录

```text
D:\Android\SDK_WSL\
├─ cmdline-tools\
│  └─ latest\
├─ ndk\
│  └─ 26.3.11579264\
├─ cmake\
│  └─ 3.22.1\
├─ platform-tools\
└─ omvll\
   └─ 1.8.0\
      ├─ omvll_ndk_r26d.so
      └─ config.py
```

版本对应关系：

| 组件 | 版本 |
| --- | --- |
| Android NDK | r26d |
| NDK 版本号 | `26.3.11579264` |
| CMake | `3.22.1` |
| O-MVLL | `1.8.0` |
| NMMP Android ABI | `arm64-v8a` |
| Android API | `21` |

## 3. 安装 WSL 基础依赖

进入 Kali WSL：

```powershell
wsl.exe -d kali
```

安装基础工具：

```bash
sudo apt update
sudo apt install -y openjdk-17-jdk curl unzip git python3 file
```

检查 Java：

```bash
java -version
javac -version
```

## 4. 准备 Android Command-line Tools

创建 SDK 目录：

```bash
mkdir -p /mnt/d/Android/SDK_WSL/cmdline-tools
```

从 Android 官方页面下载 Linux 版 Command-line Tools：

```text
https://developer.android.com/studio#command-tools
```

必须下载文件名包含 `commandlinetools-linux` 的压缩包，不能下载 Windows 版本。

在 WSL 中解压后，最终目录必须是：

```text
/mnt/d/Android/SDK_WSL/cmdline-tools/latest/bin/sdkmanager
/mnt/d/Android/SDK_WSL/cmdline-tools/latest/lib/
/mnt/d/Android/SDK_WSL/cmdline-tools/latest/source.properties
```

常见错误目录如下，需要避免：

```text
cmdline-tools/latest/cmdline-tools/bin/sdkmanager
```

## 5. 配置 WSL 环境变量

编辑 Kali 用户的 `~/.bashrc`：

```bash
nano ~/.bashrc
```

在文件末尾添加：

```bash
# Android Linux SDK，文件实际保存在 Windows D 盘。
export ANDROID_HOME=/mnt/d/Android/SDK_WSL
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/26.3.11579264"
export CMAKE_PATH="$ANDROID_HOME/cmake/3.22.1"

# O-MVLL。
export OMVLL_HOME="$ANDROID_HOME/omvll/1.8.0"
export OMVLL_PLUGIN="$OMVLL_HOME/omvll_ndk_r26d.so"
export OMVLL_CONFIG="$OMVLL_HOME/config.py"

# 构建最多使用 6 个并行任务。
export CMAKE_BUILD_PARALLEL_LEVEL=6

export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$CMAKE_PATH/bin:$PATH"
```

让配置立即生效：

```bash
source ~/.bashrc
```

检查路径：

```bash
printf '%s\n' "$ANDROID_HOME"
printf '%s\n' "$ANDROID_NDK_HOME"
printf '%s\n' "$CMAKE_PATH"
printf '%s\n' "$OMVLL_PLUGIN"
```

不要将 `ANDROID_SDK_HOME` 设置为 SDK 安装目录。当前 Android 工具使用 `ANDROID_HOME` 指定 SDK；`ANDROID_SDK_HOME` 是旧工具用于定位 `.android` 用户配置目录的变量。

## 6. 安装 Linux 版 NDK 和 CMake

接受 Android SDK 许可：

```bash
sdkmanager --sdk_root="$ANDROID_HOME" --licenses
```

安装指定版本：

```bash
sdkmanager --sdk_root="$ANDROID_HOME" \
  "ndk;26.3.11579264" \
  "cmake;3.22.1" \
  "platform-tools"
```

如需 Android build-tools，可额外安装：

```bash
sdkmanager --sdk_root="$ANDROID_HOME" "build-tools;35.0.0"
```

NMMP 的 native 编译本身主要依赖 NDK 和 CMake，通常不需要 build-tools。

## 7. 验证 NDK 和 CMake

验证 Clang：

```bash
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" --version
```

验证 CMake：

```bash
"$CMAKE_PATH/bin/cmake" --version
```

确认安装的是 Linux 文件：

```bash
file "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
file "$CMAKE_PATH/bin/cmake"
```

输出应包含：

```text
ELF 64-bit LSB
```

如果输出包含 `PE32` 或 `Windows`，说明错误地安装或复制了 Windows 版本。

执行完整性检查：

```bash
test -x "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" &&
echo "NDK OK"

test -x "$CMAKE_PATH/bin/cmake" &&
echo "CMake OK"
```

## 8. 安装 O-MVLL

从 O-MVLL 1.8.0 Release 下载对应 NDK r26d 的插件：

```text
omvll_ndk_r26d.so
```

保存到：

```text
D:\Android\SDK_WSL\omvll\1.8.0\omvll_ndk_r26d.so
```

在 WSL 中对应：

```text
/mnt/d/Android/SDK_WSL/omvll/1.8.0/omvll_ndk_r26d.so
```

检查插件：

```bash
file "$OMVLL_PLUGIN"
ldd "$OMVLL_PLUGIN"
```

`file` 应显示 Linux ELF 共享库。`ldd` 如果出现 `not found`，需要先补齐对应依赖，不能直接接入 NMMP。

O-MVLL 使用 Python 配置控制保护范围。最小配置文件保存在：

```text
D:\Android\SDK_WSL\omvll\1.8.0\config.py
```

初次接入时应使用最小配置，仅验证插件可以被 Clang 加载；不要立即对 NMMP 解释器热路径启用所有混淆。

## 9. 验证 O-MVLL 能被 NDK Clang 加载

设置 Clang 路径：

```bash
export NMMP_CLANG="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
```

先检查插件文件：

```bash
test -f "$OMVLL_PLUGIN" && echo "O-MVLL plugin found"
```

再使用一个最小 C 文件进行插件加载测试。测试成功的判断标准是：

- Clang 没有报告插件格式错误。
- 没有出现 Python 运行库缺失。
- 没有出现 LLVM 版本不匹配。
- 能正常生成 Android arm64 目标文件。

NDK r26d 与对应的 `omvll_ndk_r26d.so` 必须配套使用。不要将 r26d 插件加载到 Windows Clang，或随意加载到其他 NDK 版本。

## 10. NMMP 构建路径建议

### 方案 A：最少占用 WSL 虚拟磁盘

全部文件放在 D 盘：

```text
/mnt/d/Android/SDK_WSL        SDK、NDK、CMake、O-MVLL
/mnt/e/OtherProject/.../nmmp  NMMP 源码
/mnt/d/.../build              CMake、Ninja 中间文件
```

优点：

- WSL 虚拟磁盘占用最小。
- Windows 可以直接访问所有文件。

缺点：

- Linux 工具频繁访问 `/mnt/d`、`/mnt/e` 时，编译 I/O 会比 WSL ext4 慢。

### 方案 B：推荐折中

工具链和源码保存在 Windows 盘，只将临时编译目录放在 WSL：

```text
/mnt/d/Android/SDK_WSL        SDK、NDK、CMake、O-MVLL
/mnt/e/OtherProject/.../nmmp  NMMP 源码
/tmp/nmmp-build               临时CMake、Ninja中间文件
```

编译完成后将 `.so` 和 APK 复制回 Windows 路径，再删除 `/tmp/nmmp-build`。

优点：

- 工具链不会增加 WSL 虚拟磁盘体积。
- 大量临时小文件在 Linux 文件系统中构建，速度通常更好。
- 临时目录可以在构建完成后清理。

## 11. WSL 虚拟磁盘占用说明

将 SDK、NDK、CMake 和 O-MVLL 放到 `D:\Android\SDK_WSL` 后，这些文件不会写入 Kali 的 ext4.vhdx。

但以下内容仍会占用少量 WSL 虚拟磁盘：

- `apt` 安装的 Java、Python、Git、unzip 等软件。
- `~/.bashrc` 和用户配置。
- `/tmp` 中尚未清理的构建文件。
- Java、Gradle、Python 等工具在用户目录生成的缓存。

如果目标是严格控制体积，需要定期检查：

```bash
du -sh ~/.gradle ~/.cache /tmp 2>/dev/null
```

不要在未确认具体路径前执行递归删除。

## 12. 最终检查清单

开始修改 NMMP 构建代码前，应满足：

- [ ] `ANDROID_HOME` 为 `/mnt/d/Android/SDK_WSL`
- [ ] NDK 版本为 `26.3.11579264`
- [ ] NDK Clang 是 Linux ELF 文件
- [ ] CMake 版本为 `3.22.1`
- [ ] O-MVLL 插件为 `omvll_ndk_r26d.so`
- [ ] O-MVLL 插件是 Linux ELF 共享库
- [ ] `ldd` 没有关键依赖缺失
- [ ] O-MVLL 可以被 NDK Clang 加载
- [ ] Windows 版与 Linux 版 SDK 目录完全分开
- [ ] 已决定中间构建目录放 `/mnt/d`、`/mnt/e` 还是 `/tmp`

环境验证全部通过后，再修改 NMMP 的 `BuildNativeLib` 和 CMake 配置接入 `-fpass-plugin`，这样能把“环境问题”和“NMMP 接入问题”分开排查。

## 13. 参考资料

- Android SDK 环境变量：<https://developer.android.com/tools/variables>
- Android sdkmanager：<https://developer.android.com/tools/sdkmanager>
- Android Command-line Tools：<https://developer.android.com/studio#command-tools>
- WSL 文件系统性能：<https://learn.microsoft.com/windows/wsl/filesystems>
- WSL 与 Windows 文件互操作：<https://learn.microsoft.com/windows/wsl/interop>
- O-MVLL：<https://github.com/open-obfuscator/o-mvll>

