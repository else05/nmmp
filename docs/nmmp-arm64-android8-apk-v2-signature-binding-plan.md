# NMMP ARM64 / Android 8 APK v2 签名绑定增强方案

## 1. 文档状态

- 状态：已按方案实现；完整攻击场景回归仍需在 API 26 / 27 目标应用上执行。
- 目标 ABI：仅 `arm64-v8a`。
- 目标系统：仅 Android 8.0 / 8.1（API 26 / 27）。
- 目标产物：NMMP 处理后的 APK。
- 本方案不修改 AAB、AAR 流程。

## 2. 目标

NMMP 处理 APK 时读取输入 APK 的唯一 signer 证书，将证书 SHA-256 经过固定异或编码后编译进每个 ARM64 SO。

运行时必须满足以下三方一致：

```text
SO 中异或编码的预期证书 SHA-256
        == PackageManager 返回的证书 SHA-256
        == 从真实 base.apk v2 Signing Block 提取的证书 SHA-256
```

完整运行顺序固定为：

```text
还原 SO 内的预期证书摘要
        ↓
读取 PackageManager.signatures
        ↓
PM 证书摘要与预期摘要比较
        ↓
ARM64 原始 syscall 打开真实 base.apk
        ↓
严格解析 APK Signature Scheme v2
        ↓
v2 signer 证书摘要与预期摘要、PM 摘要比较
        ↓
使用 v2 证书 DER 恢复 VM seed
        ↓
激活 VM 并注册 native 方法
```

任一环节失败都不得激活 VM，也不得回退到较弱的数据来源。

## 3. 前置约束

1. 输入 APK 必须已经签名，并且只有一个 signer。
2. NMMP 处理后的 APK 必须使用与输入 APK 相同的证书再次签名。
3. 最终 APK 必须包含 APK Signature Scheme v2。
4. 运行设备必须是 ARM64，并运行 Android API 26 或 API 27。
5. 不支持签名轮换、多 signer、仅 v1 签名或仅 v3 签名。
6. 预期签名身份使用 `SHA-256(X509Certificate DER)` 表示，不在 SO 中保存完整证书。

当前 NMMP 在处理后输出未签名 APK，随后由外部工具签名。因此，输入 APK signer 和最终 APK signer 不一致时，合法 APK 也会拒绝激活。首版不增加新的签名证书配置入口；构建和发布流程必须保证两次使用同一证书。

## 4. 威胁模型

本方案针对以下攻击：

1. 攻击者修改 NMMP 处理后的 APK 并使用自己的证书重新签名。
2. 攻击者使用 `ApkSignatureKiller` 修改 Android 8 的 `PackageManager.getPackageInfo(..., GET_SIGNATURES)` 返回值。
3. 攻击者使用 `ApkSignatureKillerEx` 修改 `PackageInfo.CREATOR`，把 `PackageInfo.signatures[0]` 替换为原证书。
4. 攻击者使用 `ApkSignatureKillerEx` hook libc 的 `open`、`open64`、`openat`、`openat64`，尝试把真实 `base.apk` 重定向到内置的 `origin.apk`。

本方案不承诺抵抗能够任意修改进程内存、patch SO 控制流、hook 内核 syscall 或修改系统内核的攻击者。固定异或也只是定位阻碍，不是密钥保护。

## 5. 构建期签名数据

### 5.1 读取输入证书

继续使用 `SignatureBinding.readSingleSignerCertificate()`：

1. 使用 `ApkVerifier` 验证输入 APK。
2. 要求验证成功。
3. 要求 signer 证书数量恰好为 1。
4. 取得证书的 X.509 DER 字节。

### 5.2 生成预期摘要

构建期计算：

```text
expectedSignerDigest = SHA-256(signerCertificateDer)
```

摘要固定为 32 字节。

### 5.3 固定异或编码

在 native 模板中定义固定的 32 字节 mask：

```text
FIXED_SIGNER_XOR_MASK[32]
```

生成 SO 配置时写入：

```text
encodedSignerDigest[i] = expectedSignerDigest[i]
                         XOR FIXED_SIGNER_XOR_MASK[i]
```

生成的 `VmCodecConfig.h` 只包含 32 字节的 `encodedSignerDigest` 数值数组，不包含：

- 完整证书 DER；
- Base64 证书；
- 原始 SHA-256；
- SHA-256 hex 字符串。

mask 是固定且可被逆向恢复的。它的作用仅是避免攻击者使用已知证书摘要直接搜索 SO 字节，不应被视为秘密。

### 5.4 保持现有 VM seed 绑定

现有构建期算法保持不变：

```text
bindingMask = deriveMask(packageName, signerCertificateDer, buildId)
seedData = buildSeed XOR bindingMask
```

新增的摘要常量只用于显式身份比较，不替代现有证书参与 VM seed 的机制。

## 6. Android 8 PackageManager 检查

Android 8 不需要 Android 9 引入的 `SigningInfo` 分支。运行时只保留以下路径：

```text
PackageManager.getPackageInfo(packageName, GET_SIGNATURES)
PackageInfo.signatures
Signature.toByteArray()
```

其中 `GET_SIGNATURES` 固定为 `0x00000040`。

运行步骤：

1. 检查 `SDK_INT`，只接受 26 或 27。
2. 取得当前包名，并继续与构建期 `NMMP_VM_PACKAGE_NAME` 比较。
3. 调用 `getPackageInfo()`。
4. 要求 `PackageInfo.signatures` 非空且长度恰好为 1。
5. 调用唯一 `Signature` 的 `toByteArray()` 取得证书 DER。
6. 在 native 中计算 `pmDigest = SHA-256(pmCertificateDer)`。
7. 在栈上异或还原 `expectedSignerDigest`。
8. 使用常量时间比较 `pmDigest == expectedSignerDigest`。
9. 比较失败立即返回，不执行 APK 文件读取。

普通重签 APK 会在这一阶段快速失败。签名伪装工具可能令这一阶段通过，因此它不能作为最终信任来源。

## 7. ARM64 原始 syscall 读取

### 7.1 只实现 AArch64

不增加 ARM32、x86 或 x86_64 分支。原始 syscall 包装只实现 AArch64 ABI：

- syscall 号放入 `x8`；
- 参数使用 `x0` 至 `x5`；
- 执行 `svc #0`；
- 从 `x0` 读取返回值并处理 Linux 负 errno。

syscall 号应使用 NDK 头文件中的 `__NR_*` 宏，不在业务代码中散落裸数字。

### 7.2 最小 syscall 集合

仅实现本方案需要的调用：

- `openat`
- `read`
- `pread64`
- `fstat`
- `close`

先使用原始 syscall 打开并读取 maps：

```text
mapsPath = "/proc/self/maps"
openat(AT_FDCWD, mapsPath, O_RDONLY | O_CLOEXEC, 0)
read(mapsFd, ...)
```

从 maps 得到目标路径后，再使用原始 syscall 打开 APK：

```text
openat(AT_FDCWD, mapsBaseApkPath, O_RDONLY | O_CLOEXEC, 0)
```

不得调用 libc 的 `open`、`open64`、`openat`、`openat64`、`fopen`、`read` 或 `pread`，避免经过可被 xhook 修改的 PLT/GOT 路径。

### 7.3 直接从 `/proc/self/maps` 获取 APK 路径

已在当前连接设备上确认：

```text
SDK：27
ABI：arm64-v8a
包名：org.savior.sync
maps 路径：/data/app/org.savior.sync-r8Gcbr64Fn_CGNo_m4tZUA==/base.apk
```

该路径在 maps 中出现两次，但两条记录指向完全相同的 APK。按路径去重后可以得到唯一目标，因此不再读取或比较 `ApplicationInfo.sourceDir`、`getPackageCodePath()` 和 `/proc/self/fd/<fd>`。

运行时按以下最小规则定位：

1. 使用 ARM64 原始 `openat` 打开 `/proc/self/maps`。
2. 使用 ARM64 原始 `read` 流式解析 maps，不要求一次读完整文件。
3. 只保留形如 `/data/app/<packageName>-*/base.apk` 的路径。
4. `<packageName>` 必须与构建期 `NMMP_VM_PACKAGE_NAME` 完全匹配。
5. 对重复映射行按完整路径去重。
6. 去重结果必须恰好只有一个路径；没有结果或出现多个不同路径都失败。
7. 使用原始 `openat` 直接打开这个唯一路径。
8. 使用原始 `fstat` 要求目标是普通文件，且大小足以容纳 ZIP EOCD 和 APK Signing Block。

`ApkSignatureKillerEx` 保存的 `origin.apk` 位于应用私有数据目录，不符合 maps 目标路径规则。后续解析始终复用这个 APK FD，并通过原始 `pread64` 按偏移读取。

## 8. APK Signature Scheme v2 解析

### 8.1 解析范围

解析器只读取 APK 尾部和必要的 Signing Block 区间，不遍历 APK 全部内容。

步骤如下：

1. 从文件尾部读取最多 `22 + 65535` 字节，查找唯一有效 ZIP EOCD。
2. 校验 EOCD 注释长度与文件末尾一致，不接受 EOCD 后附加数据。
3. 读取并校验 Central Directory offset 和 size。
4. 要求 Central Directory 的结束位置紧邻 EOCD。
5. 从 Central Directory 前方定位 APK Signing Block footer。
6. 校验头尾 size 字段一致。
7. 校验 `APK Sig Block 42` magic。
8. 要求 Signing Block 紧邻 Central Directory。
9. 遍历 ID-value pairs，所有长度计算使用显式溢出检查。
10. 要求 v2 ID `0x7109871a` 存在且只出现一次。
11. 解析 v2 signers、signed data 和 certificates。
12. 要求 signer 数量恰好为 1。
13. 要求证书序列非空，提取第一张证书的完整 DER 字节。
14. 拒绝所有截断、越界、重复 ID、长度为零或结构剩余字节异常的输入。

禁止通过全文搜索 v2 ID、证书前缀或已知摘要来代替结构化解析。

### 8.2 本阶段不做的验证

为控制启动开销，首版不实现：

- APK 全文件的 v2 分块内容摘要计算；
- RSA/ECDSA 对 signed data 的验证；
- v3 signer 和签名轮换 lineage；
- v1 签名回退。

本方案依赖 Android 8 安装阶段已经验证 APK v2 签名有效，运行时只独立确认实际安装 APK 的 signer 身份。没有合法 v2 块时必须失败，不能回退到 PM 证书。

## 9. 三方比较和 VM 激活

从真实 APK 提取证书后计算：

```text
apkDigest = SHA-256(apkV2CertificateDer)
```

必须依次满足：

```text
pmDigest  == expectedSignerDigest
apkDigest == expectedSignerDigest
apkDigest == pmDigest
```

最后一个比较在逻辑上冗余，但保留它可以明确表达三方一致关系，也便于测试和诊断。

全部通过后，必须使用 v2 Signing Block 中提取的证书 DER，而不是 PM 返回的 DER，计算运行时绑定值：

```text
bindingMask = deriveMask(packageName, apkV2CertificateDer, buildId)
vmCodecActivate(bindingMask)
```

只有 `vmCodecActivate()` 成功后才能继续 resolver 初始化和 native 方法注册。

## 10. 失败和内存处理

1. 所有检查采用 fail-closed。
2. PM 检查失败时不读取 APK，降低正常攻击拒绝路径的成本。
3. syscall、maps 定位、v2 解析或摘要比较任一失败时，初始化状态设置为永久失败。
4. 不允许同一进程再次尝试激活。
5. 摘要比较使用常量时间实现。
6. 比较结束后清理：
   - 还原后的预期摘要；
   - PM 摘要；
   - APK 摘要；
   - PM 证书 DER 临时缓冲区；
   - v2 证书 DER 临时缓冲区。
7. 不在 release 日志中输出证书、摘要、APK 绝对路径或具体失败阶段。
8. 不得在 v2 检查失败时回退到 PM 检查结果。

## 11. 代码变更范围

### 11.1 Java 构建工具

- `nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/sign/SignatureBinding.java`
  - 增加证书 SHA-256 生成。

- `nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/ProtectionContext.java`
  - 保存预期证书摘要。
  - `TEMPLATE_VERSION` 从 2 升到 3。
  - `CODEC_VERSION` 保持不变。

- `nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/util/CmakeUtils.java`
  - 将异或后的 32 字节摘要写入 `VmCodecConfig.h`。
  - 校验新增 native 模板文件。

### 11.2 Native VM 模板

- `nmmvm/nmmvm/src/main/cpp/vm/VmBinding.cpp`
  - 移除 Android 9+ `SigningInfo` 分支。
  - 串联 PM 检查、syscall 读取、v2 解析和 VM 激活。

- `nmmvm/nmmvm/src/main/cpp/vm/include/VmCodecConfig.h`
  - 增加异或摘要占位数组。
  - 模板版本升级为 3。

- 新增小型模块：
  - `Sha256.cpp` / `Sha256.h`
  - `Arm64Syscall.cpp` / `Arm64Syscall.h`
  - `ApkV2Signer.cpp` / `ApkV2Signer.h`

- 更新：
  - `nmmvm/nmmvm/src/main/cpp/vm/CMakeLists.txt`
  - `nmm-protect/mksrc/vm/CMakeLists.txt`

- 通过 `nmm-protect/mksrc/build-src.sh` 重新生成：
  - `nmm-protect/apkprotect/src/main/resources/vmsrc.zip`

### 11.3 不修改的范围

- AAB 保护流程。
- AAR 保护流程。
- VM 指令编码算法。
- OMVLL 保护配置。
- ARM32、x86 和 x86_64 构建代码；它们不在本功能支持范围内。

## 12. 测试方案

### 12.1 构建期单元测试

1. 已知证书 DER 的 SHA-256 结果正确。
2. 固定异或编码和 native 还原结果一致。
3. 生成配置恰好包含 32 字节编码摘要。
4. 生成配置不包含完整证书、原始摘要和摘要 hex 字符串。
5. 现有 `seedData` 派生结果保持兼容。

### 12.2 v2 解析器测试

至少准备以下 APK fixture：

1. 正常 v2、单 signer APK：成功提取预期证书。
2. 仅 v1 APK：拒绝。
3. 多 signer v2 APK：拒绝。
4. EOCD 截断或注释长度错误：拒绝。
5. Central Directory offset/size 越界：拒绝。
6. Signing Block 头尾 size 不一致：拒绝。
7. Signing Block magic 错误：拒绝。
8. 重复 v2 ID：拒绝。
9. signer、signed data 或 certificate 长度越界：拒绝。
10. 文件尾部追加数据：拒绝。

### 12.3 ARM64 / Android 8 设备测试

分别在 API 26 和 API 27 的 ARM64 环境验证：

1. 使用原证书签名的正常 APK可以激活 VM。
2. 修改并使用攻击者证书重签，未安装签名伪装工具：PM 阶段失败。
3. 重签并使用旧版 `ApkSignatureKiller`：校验失败。
4. 重签并使用 `ApkSignatureKillerEx.killPM`：PM 阶段可能通过，但 v2 阶段失败。
5. 重签并同时使用 `killPM`、`killOpen`：原始 syscall 仍读取真实 `base.apk`，v2 阶段失败。
6. 最终 APK 使用与输入 APK 不同的证书：按设计失败。
7. 删除 v2 只保留 v1：按设计失败。

## 13. 实施顺序与验收点

1. 构建期摘要和异或配置
   - 验收：单元测试通过，SO 配置不存在明文证书摘要。

2. 自包含 SHA-256 和常量时间比较
   - 验收：与 Java SHA-256 测试向量一致。

3. ARM64 原始 syscall 包装
   - 验收：API 26/27 ARM64 上能从 `/proc/self/maps` 去重得到唯一目标路径并按偏移读取真实 `base.apk`；libc `openat` 被 hook 时结果不受影响。

4. 严格 v2 结构解析
   - 验收：正常 fixture 成功，全部畸形 fixture fail-closed。

5. 串联 PM、v2 和 VM seed
   - 验收：合法 APK 正常运行，普通重签在 PM 阶段失败，PM 伪装在 v2 阶段失败。

6. 更新模板和 `vmsrc.zip`
   - 验收：模板版本检查通过，只生成 `arm64-v8a` 目标时可完整编译。

7. 对两个开源签名伪装工具做设备回归
   - 验收：`killPM` 和 `killOpen` 同时启用仍无法激活 VM。

## 14. 最终成功标准

1. API 26/27 ARM64 合法 APK 的受保护方法正常执行。
2. SO 中不存在完整证书、原始证书 SHA-256 或其字符串形式。
3. 普通重签 APK 在 PM 检查阶段失败。
4. PM 返回原证书但实际 APK 为攻击者证书时，在 v2 检查阶段失败。
5. libc 文件 API 被 `ApkSignatureKillerEx` 重定向时，ARM64 原始 syscall 仍读取真实安装 APK。
6. 任一检查失败都无法恢复正确 VM seed、注册受保护 native 方法或继续执行受保护代码。
