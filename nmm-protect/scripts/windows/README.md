# Windows 原生构建与配置说明

本文说明当前单路径版本如何在 Windows 上构建保护器 JAR、部署运行文件并处理 APK。当前版本不使用 WSL 或 O-MVLL；仅支持 ARM64，编译目标为 Android API 26，运行范围为 API 26/27。

## 1. 目录与工具要求

推荐工具版本：

- JDK 21（最低应支持标准 Ed25519）；
- Android SDK：`D:\Android\SDK`；
- Android NDK：`27.0.12077973`；
- CMake/Ninja：Android SDK 内的 CMake `3.22.1`；
- Python 3，用于 Gradle 的格式一致性检查；
- 项目目录：`E:\OtherProject\safe-toolchain\nmmp`。

保护 APK 时会在 Windows 上直接调用 NDK 的 `windows-x86_64` Clang、CMake 和 Ninja。不要再配置 `OMVLL_PLUGIN`、`OMVLL_CONFIG` 或 `OMVLL_PYTHONPATH`。

## 2. env 文件格式

复制 `nmm-protect\scripts\windows\nmmp.env.example` 为自有配置，例如 `D:\config\nmmp.env`。格式为每行一个 `KEY=VALUE`，以 `#` 开头的行是注释，不要给值添加引号。脚本参数 `-e`/`--env` 可指定任意 env 文件；未指定时会尝试加载脚本同目录的 `nmmp.env`。

env 文件中的相对工具、JAR、密钥和敏感方法清单路径以 env 文件所在目录为基准。命令行传入的 APK、规则和 mapping 相对路径仍以调用脚本时的当前目录为基准。

### 工具链与部署配置

| 配置项 | 必需 | 默认值/选项 | 用途 |
| --- | --- | --- | --- |
| `JAVA_HOME` | 构建 JAR 时建议 | 无 | JDK 根目录，`build-nmmp.ps1` 会把其 `bin` 加入当前进程 PATH。 |
| `JAVA_EXE` | 否 | `java` | `nmmp.cmd` 启动保护器所用的 Java 可执行文件。 |
| `ANDROID_HOME` | 是 | `D:\Android\SDK` | Android SDK 根目录。 |
| `ANDROID_SDK_HOME` | 否 | 与 `ANDROID_HOME` 相同 | 旧代码兼容别名。 |
| `ANDROID_NDK_HOME` | 是 | `%ANDROID_HOME%\ndk\27.0.12077973` | NDK 根目录；必须包含 Windows ARM64 Clang。 |
| `CMAKE_PATH` | 是 | `%ANDROID_HOME%\cmake\3.22.1` | CMake 根目录；同时使用其中的 Ninja。 |
| `CMAKE_BUILD_PARALLEL_LEVEL` | 否 | `6` | CMake/Gradle 构建并行度。 |
| `NMMP_PROJECT_ROOT` | 构建 JAR 时必需 | 当前机器项目路径 | 源码仓库根目录。 |
| `NMMP_JAR` | 处理 APK 时必需 | `D:\Android\SDK_WSL\nmmp\vm-protect.jar` | `nmmp.cmd` 实际执行的 JAR。目录名保留历史名称，但构建过程不再使用 WSL。 |
| `NMMP_DEPLOY_JAR` | 否 | 无 | `build-nmmp.ps1` 的部署目标；设置后会同时更新同目录 `tools\vmsrc.zip`。 |

### 保护行为配置

| 配置项 | 必需 | 默认值/选项 | 用途 |
| --- | --- | --- | --- |
| `NMMP_ARTIFACT_PRIVATE_KEY` | 否 | 脚本旁 `artifact-private.pk8` | 为最终 DEX/核心 SO 清单签名；私钥只在构建机读取。 |
| `NMMP_ARTIFACT_PUBLIC_KEY` | 否 | 脚本旁 `artifact-public.spki` | 公钥会编译进受保护 native 内层，用于运行时验签。 |
| `NMMP_PROTECTION_PROFILE` | 否 | `enforce`；可选 `observe`/`enforce` | `observe` 只记录可疑状态，`enforce` 会拒绝后续受保护调用。Windows 脚本默认使用 `enforce`。 |
| `NMMP_DIAGNOSTICS` | 否 | `false`；可选 `true`/`false` | 是否编译详细 logcat 和应用私有 `check.log` 诊断。生产版本保持 `false`。 |
| `NMMP_SENSITIVE_METHODS_FILE` | 否 | 空 | UTF-8 精确 DEX 方法清单，最多 20 项；非空项必须全部匹配并转换。 |
| `NMMP_TEST_SEED` | 否，仅测试 | 空；16 位十六进制 | 固定 Java 生成器随机流，用于性能对照。生产构建必须留空。 |

`nmmp.cmd` 会把配置映射到保护器实际读取的接口：

| env 配置 | 保护器接口 |
| --- | --- |
| `NMMP_ARTIFACT_PRIVATE_KEY` | `-Dnmmp.artifact.privateKey=...` |
| `NMMP_ARTIFACT_PUBLIC_KEY` | `-Dnmmp.artifact.publicKey=...` |
| `NMMP_PROTECTION_PROFILE` | `-Dnmmp.protectionProfile=...`；源码也接受同名环境变量。 |
| `NMMP_DIAGNOSTICS` | `-Dnmmp.diagnostics=true/false` |
| `NMMP_SENSITIVE_METHODS_FILE` | `-Dnmmp.sensitiveMethodsFile=...` |
| `NMMP_TEST_SEED` | 保持为环境变量；手工运行时可用优先级更高的 `-Dnmmp.testSeed=...`。 |

两项密钥配置必须同时为空或同时填写。两者都为空时，脚本会显示醒目警告并使用与 `nmmp.cmd` 同目录的默认密钥对；只填写其中一项会立即失败，防止错配。默认密钥方便本机固定构建，但所有使用它的 APK 共享同一构建身份。发布项目建议显式配置各自独立的密钥对并妥善备份私钥。

敏感方法格式示例：

```text
Lcom/example/License;->verify(Ljava/lang/String;)Z
Lcom/example/Crypto;->derive([B)[B
```

Ed25519 密钥可用 OpenSSL 生成；私钥文件本身不要放入仓库：

```powershell
openssl genpkey -algorithm ED25519 -outform DER -out artifact-private.pk8
openssl pkey -inform DER -in artifact-private.pk8 -pubout -outform DER -out artifact-public.spki
```

这些清单签名密钥与 APK 发布签名证书用途不同，不能替代 APK 的 zipalign/apksigner 流程。

## 3. 在 Windows 构建并部署最新 JAR

`build-src.ps1` 会从当前 native 源码重新生成 `vmsrc.zip`。这一步不能省略：运行目录下的 `tools\vmsrc.zip` 会优先于 JAR 内资源，旧模板与新 JAR 混用会造成格式不一致。

把 `nmm-protect\scripts\windows` 中的脚本复制到 `E:\env-tool` 后执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File E:\env-tool\build-nmmp.ps1 `
  -EnvFile D:\config\nmmp.env
```

构建脚本依次执行：

1. 加载 env 配置；
2. 运行 `nmm-protect\mksrc\build-src.ps1`，以 STORED 方式重建模板 ZIP；
3. 执行 `:apkprotect:test`；
4. 执行根项目 `jar` 任务生成 fat JAR；
5. 如果配置 `NMMP_DEPLOY_JAR`，复制 JAR 和匹配的 `tools\vmsrc.zip`；
6. 对源文件与部署文件计算并比较 SHA-256。

任何一步失败都会终止，不会报告部署成功。

## 4. 使用 Windows 脚本处理 APK

```bat
E:\env-tool\nmmp.cmd app-release.apk ^
  -r convertRules.txt ^
  -m mapping.txt ^
  -e D:\config\nmmp.env
```

参数：

- `-r` / `--rules`：转换规则。未指定且当前目录存在 `convertRules.txt` 时自动使用；两者都没有时匹配全部普通方法（构造和静态初始化除外）。
- `-m` / `--mapping`：R8/ProGuard mapping；必须同时有规则文件。
- `-e` / `--env`：选择 env 配置文件。
- `-h` / `--help`：显示帮助。

规则示例：

```text
class com.example.security.* { *; }
class * extends android.app.Activity
class com.example.Api {
  verify*;
}
```

脚本先校验 Java、Windows NDK Clang、CMake、Ninja、JAR、密钥和可选文件，再运行保护器。输出会移动到调用目录并命名为：

```text
<原文件名>_protectyyyyMMddHHmmss.apk
```

生成 APK 尚未完成发布签名；之后仍需执行 zipalign 和 apksigner，并在 API 26/27 ARM64 设备上做启动、关键业务和受保护方法回归。

## 5. 已移除或不再生效的旧配置

当前版本不再支持或读取以下构建模式：

- O-MVLL 的 `OMVLL_PLUGIN`、`OMVLL_CONFIG`、`OMVLL_PYTHONPATH`；
- `NMMP_VM_DECODE_MODE` / `-DvmDecodeMode`；
- `NMMP_PRIVATE_LINKER`；
- `NMMP_PRIVATE_STAGE0_VM`；
- codec2、legacy VM 和直接装载解释器 SO 的路径。

当前路径固定为 codec3、按需解码、私有 loader 和 stage0 VM。旧 env 变量即使仍存在也不会切换回旧实现。

## 6. 常见故障

- `JAR not found`：检查 `NMMP_JAR`，或先运行构建部署脚本。
- `Windows NDK Clang not found`：`ANDROID_NDK_HOME` 指向了 WSL/Linux NDK 或错误版本。
- 缺少 artifact key：配置两项 Ed25519 文件路径，且确认格式分别为 PKCS#8 DER 和 X.509 SPKI DER。
- 敏感方法未转换：描述符与最终 DEX/R8 mapping 不一致；先核对 mapping 和规则覆盖范围。
- 修改 native 源码后行为仍旧：重新运行 `build-src.ps1`，并确认部署目录的 `tools\vmsrc.zip` 哈希同步更新。
