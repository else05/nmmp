# 私有 loader 验证结果

日期：2026-09-12。**本文是按需解码和 stage0 VM 接入前的私有 loader 阶段快照。** 当时指定 APK 的 API27 实机集成验证通过，全量 ACCEPTANCE 未完成，开关默认 OFF。随后已完成按需/token/root native VM 与 stage0 VM 组合，见 [P5 实施记录](../ON_DEMAND_WORK.md#p5-当前证据)；当前产物及未通过项见 [S5 结果](../S5_RESULTS.md)。本报告不将启动成功或离线方法回验等同于全部业务语义验收。

## 产物与范围

该阶段 JAR：`nmm-protect/build/libs/vm-protect-2026-09-12-1804.jar`。
该阶段安装的开启版：`nmm-protect/build/validation-20260912-private-loader/final/protected-signed.apk`。
关闭对照：同目录的 `control/protected-signed.apk`。所有文件仅保留本地。

输入为用户指定的 `MdoHelper_v1.6.0M_202609092340_release.apk`，原 `convertRules.txt` 和对应 release mapping，未缩减方法。仍为 `org.savior.sync`、223 个转换类、526 个方法、原 release signer。原 APK 未修改，使用 `adb install -r` 覆盖测试，保留应用数据。

设备 `192.168.6.118:5555`，`g6pa_ica2_for_arm64`，Android API27 / ARM64。正式 A/B 均用 WSL NDK **26.3.11579264**、Clang **17.0.2**、CMake **3.22.1**、API26 编译目标、Release/O3、O-MVLL **1.8.0**。两个构建复用相同生成代码、opcode 配置、codec seed 和 DEX；A/B 的 APK 内容仅 `lib/arm64-v8a/libc++_en.so` 不同。

为分离 C++ 可行性调整，两边使用相同的无 native exception/RTTI、SysV hash、普通 RELA、局部绑定及归档隐藏策略。A 是显式 OFF 的受控基线，不是此前 S0 的 API21 历史构建。O-MVLL 是两次独立编译，插件随机 seed 未冻结；不能将微小差异解释为 loader 带来加速。

最终 APK SHA-256：`e77076035ca02dad8ac4ef585a1cd0f408d7df989a3d119a9bcc734830fd8548`。
原/开启版 signer SHA-256 均为 `715519182201a4bfc56806fe1a2fea6084130d548bc8768495bb2f9cd0970ae2`。
完整输入、规则、mapping、JAR、SO 摘要见 [manifest](validation/20260912/manifest.json)。

## 已验证的正确性

| 检查 | 结果及实际覆盖 |
| --- | --- |
| Java 构建/测试 | 受影响构建通过；现有 arsc 16、apkprotect 32 项无失败。后续仅模板资源更新的构建保留 up-to-date 结果，不重复计为新增测试 |
| 原 Android 开发 VM | Debug/Release 构建通过，确认 OFF 分支兼容；NDK27 的开发工程与 NDK r26d 正式模板分开记录 |
| Python 打包测试 | 5 组通过：LOAD 拆分还原、支持的 relocation、未知特性及越界拒绝、初始化数组对齐、版本表链/flags/hash/index 损坏 |
| 密码/封装 | RFC8439 §2.8.2；JDK 生成的 golden 由 Monocypher 和独立 cryptography 验证；逐字节篡改、全部截断位置、尾随数据、错误 key 均拒绝 |
| Host sanitizer | ASan/UBSan 下 crypto_envelope、loader_content、loader_once 三组通过，含 2000 个确定性变异输入、分配/mmap/mprotect 失败与清理 |
| ARM64 独立映射 | 实机执行映射代码；NONE/RELATIVE/ABS64/GLOB_DAT/JUMP_SLOT、BSS、RX/RELRO/空洞权限通过 |
| 系统/私有模块对照 | 同一测试 ELF 的构造器顺序、BSS、公共依赖、浮点结果一致；依赖解析/版本失败清理通过；构造器后拒绝危险卸载 |
| strong/weak | 默认和指定版本查询同时缺失时 strong 失败，weak 的 S=0 重定位通过；不等版本地址注入被拒绝 |
| 并发/重入 | 16 个首次并发线程仅初始化一次；回调不持全局锁；同线程重入及失败后重试均保持永久失败 |
| Bootstrap/JNI | 真实生成器两分支的 14 次测试运行通过：尺寸/build ID/GetEnv/缓存与 setup 异常、顺序、延后 Context 激活及激活失败不重试。JNI/cache/binding 使用测试桩；不是完整未绑定业务 APK 验证 |
| 指定 APK | 最终签名产物的 526 个方法代码/tries 逐字节回验，native 方法集合精确一致；API27 正常签名启动、主 Activity 可见/恢复通过 |

离线方法结果及范围见 [all-method-audit.json](validation/20260912/all-method-audit.json)，它不证明所有方法的运行路径均被执行。SDK v2 signer 校验的生产逻辑保留，没有用内层解密成功替代签名验证。

复核发现并修正了未对齐 INIT_ARRAY、映像外 RELRO、失败标志发布窗口及仅按 basename 判断 provider 四项问题。最终版本均有代码检查；前两项有定向坏输入回归。当前公共依赖绝对路径装载、canonical 路径及文件身份检查、同句柄版本等价检查均在实际正常装载中通过。

## 启动对照

ABBA 顺序，10 个正式区组，每版 **20** 个有效样本，另每版 2 个预热样本；无效样本 **0**。每次覆盖安装后核对设备 APK SHA-256，再确认旧 PID 退出、启动新进程、主 Activity resumed、同 PID 主窗口 HAS_DRAWN/visible，并观察进程持续存活。没有清除应用数据。

| 指标 | A：OFF | B：ON |
| --- | ---: | ---: |
| ThisTime 中位数 | 2341 ms | 2335.5 ms |
| P95（nearest rank） | 2479 ms | 2386 ms |
| 最小 / 最大 | 2275 / 2512 ms | 2293 / 2394 ms |

B/A 中位数差异 **−0.23%**，本批未观察到超过 10% 的系统启动耗时回退，可视为噪声范围内相当。原始样本见 [startup-attempts.jsonl](validation/20260912/startup-attempts.jsonl)、[汇总](validation/20260912/startup-summary.json)。

端点是 `am start -W ThisTime` 加主窗口状态核验，尚不是应用内埋点确认的“页面可交互”端点。覆盖安装和独立 O-MVLL 编译也是本批限制；未完成首次激活、热方法或真实业务热点测试。因此不能据此宣布完整性能门槛通过。

22 次 B 启动（含预热）的低频阶段中位数：

| 阶段 | 中位数 |
| --- | ---: |
| 认证解密 + 解压 | 11.045 ms |
| 校验/映射/依赖/重定位/权限及 scratch 释放 | 9.661 ms |
| 构造器 | 0.033 ms |
| bootstrap/cache/setup | 0.492 ms |

这些是合并阶段计时，不是逐 relocation 日志；Context 签名绑定时间未单独拆分。各阶段中位数不可相加冒充总耗时中位数。见 [loader-phases.json](validation/20260912/loader-phases.json)。

## 大小、内存与生命周期

| 项目 | A | B |
| --- | ---: | ---: |
| 已签名 APK | 9,277,414 bytes | 9,248,742 bytes |
| 打入 APK 的 SO | 887,656 bytes | 603,944 bytes |
| 末区组同观察点 PSS（各 2 次） | 53,835 / 53,565 KiB | 54,902 / 56,332 KiB |

这 2 次 PSS 的中位数差为 **+1,917 KiB**，样本少且不是峰值；未设定内存回退门槛，不能据此宣称内存验收通过。native heap 原表及各次值保存在 manifest 和 meminfo 日志。

最终 B 载荷 574,162 bytes，解压内容 815,942 bytes，匿名映像 **872,448 bytes**。本地带符号 inner 为 1,948,352 bytes，不进入 APK。APK 没有独立 inner 条目；outer 普通符号表未发现 VM/wrapper 实现，也没有 DT_NEEDED 指向 inner。实际映像匿名 RX/只读 RELRO/RW 区域无 W+X，装载 scratch 不由模块保留。

最终进程 **302.2 秒、20 次**驻留观察中 PID 一致、查询错误为 0，未观察到 Java/native 崩溃、InternalError、UnsatisfiedLinkError 或 JNI 错误；PSS 为 55,786–57,071 KiB。此为短期观察，不是无泄漏或长期稳定性的证明。另执行最终 APK 的 5 次 HOME/恢复核验，原始结果见验证目录。

## 该阶段尚未完成

- API26 ARM64 实机，以及 APK 直接映射/提取两种布局的完整矩阵；当前设备使用提取后的 `lib/arm64` 外层。
- 真实错误 signer、映射 APK 定位失败的实机负向路径；测试桩返回 binding 失败不等同于这些测试。
- 全部规则方法的业务语义、真实 JNI 异常/对象/数组等完整差分、代表性热点、首次激活、应用内交互端点。
- 装载临时峰值、RSS/缺页、长期 native heap；native unwinder 对匿名模块的自动发现/完整跨帧展开。
- 上级按需解码/native VM 与低频 stage-0 VM 组合；当前只提供独立 key 份额恢复，无循环装载依赖。

这些缺口使默认 ON 和完整 P4/P5 验收保持未完成。可通过 [README](README.md) 的显式开关继续测试，现有功能不自动改为开启。

验证快照位于 [validation/20260912](validation/20260912/manifest.json)，完整临时构建与原始窗口/maps 日志留在 `nmm-protect/build/validation-20260912-private-loader`。未上传 APK、SO、签名材料或日志附件，未提交 git commit，保留用户原有 `.gitattributes`。
