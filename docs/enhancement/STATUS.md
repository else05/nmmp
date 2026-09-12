# NMMP 按需解码升级实施状态

更新：2026-09-13。**S1–S4 及私有 linker P5 已实现并完成主机/ARM64 API27 功能验证；S5 三组对照已采集并重算，但性能验收未通过；最终擦除修复的API27功能复查已通过。默认仍为 legacy，私有 linker 默认 OFF。**

当前实现、526 方法回验和组合 APK 证据见 [ON_DEMAND_WORK.md](ON_DEMAND_WORK.md)，使用见 [USAGE.md](USAGE.md)，格式见 [ON_DEMAND_FORMAT.md](ON_DEMAND_FORMAT.md)。性能规程见 [S5_PLAN.md](S5_PLAN.md)，实测及未通过项见 [S5_RESULTS.md](S5_RESULTS.md)，最终源码修复、产物及定向复查见 [POST_REVIEW.md](POST_REVIEW.md)。以下 S0 内容为历史基线，不代表当前源码尚未实施。

依据 `E:/OtherProject/360_jiagu/360_enhance/` 的 README、IMPLEMENTATION、RE_EVIDENCE、ACCEPTANCE。四份输入文档的 SHA-256 保存在 [plan-hashes.json](runs/20260912-152656/S0/plan-hashes.json)。

## 已完成的 S0 工作

* 确认 HEAD 为 `9dce1bd5b6df31ac098fc26010934b659ef55cfa`，codec 2/template 3。初始仅有用户既存的未跟踪 `.gitattributes`；未改动该文件。
* 完成 [指令读取审计](READ_ACCESS_AUDIT.md)，记录 FETCH 预读、payload、异常、PC 和生成器的特殊处理要求。
* Java 重新执行构建及测试：arsc 16 项、apkprotect 32 项，共 48 项，失败/错误/跳过均为 0；18 个任务实际执行。保留 [JUnit 原始结果](runs/20260912-152656/S0/java-tests.json) 和 XML。
* 修复现有 native 测试 CMake 缺少 `vm/include` 的问题，仅增加 target 的 include directory。修复前编译命令没有 `-I`，而 VmCodec.h 确实位于 include 子目录；现有测试直接复现并验证此修复，无需新增模拟测试。Debug、Release、ASan/UBSan 各 1 项通过。
* 现有 Android VM 工程 `:nmmvm:assembleDebug :nmmvm:assembleRelease` 构建成功。Release 的 native 配置实际是 RelWithDebInfo；该工程的 VM 为 shared，不能代替正式静态模板产物验证。
* 内嵌 `vmsrc.zip` 的 33 个文件与源码/正式 mksrc 模板逐字节一致。正式 VM 模板为 STATIC，最终 SO 为 SHARED，保留隐藏符号及 O-MVLL 参数接入。见 [模板哈希报告](runs/20260912-152656/S0/template-audit.json)。本次未重写模板。

## 工具链与复现

本次 Java 使用 `E:/Scoop/apps/corretto17-jdk/current`（Corretto 17.0.18），保护器 Gradle 7.3；Android 工程 Gradle 8.10.2、AGP 8.8.0、NDK 27.0.12077973、Windows CMake 3.22.1、android-21 编译目标。这只是现有工程的构建基线，不改变新模式的 ARM64/API 26–27 范围。
host native 使用 Kali WSL 的 GCC 15.3.0、Ninja 和系统 CMake 4.3.4。O-MVLL 本次未做产物功能测试，正式性能实验所用配置仍待指定。

从仓库根执行（每次新的验收应使用新的输出目录，避免覆盖本次证据）：

```powershell
$env:JAVA_HOME = 'E:\Scoop\apps\corretto17-jdk\current'
Set-Location nmm-protect
.\gradlew.bat :arsc:build :apkprotect:test build --offline --rerun-tasks --max-workers=5 --console=plain
Set-Location ..
wsl -d kali -- bash /mnt/e/OtherProject/safe-toolchain/nmmp/docs/enhancement/runs/20260912-152656/S0/run-native.sh
$env:ANDROID_HOME = 'D:\Android\SDK'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '5'
Set-Location nmmvm
.\gradlew.bat :nmmvm:assembleDebug :nmmvm:assembleRelease --max-workers=5 --console=plain
```

原始日志在 [S0 输出目录](runs/20260912-152656/S0/)。初次 Java 受限运行因 Gradle 缓存锁不可写未启动；获准使用现有缓存后通过。Android 初次离线构建缺少 `aapt2:8.8.0-12006047`，下载现有版本依赖后重试成功。均保留失败日志。
Java 构建仍有既有 `:jar` 对 arsc.jar 的隐式依赖警告及 Gradle 弃用警告，本次未修改这些非阻断问题。

## 后续真实 APK 验证

用户随后指定 `D:/AndroidProjects/sync-ui/app/build/outputs/apk/release/` 中的 APK 并连接设备。本轮使用 `MdoHelper_v1.6.0M_202609092340_release.apk`、同工程 convertRules/mapping 和现有 release 签名；生产保护流程沿用 WSL NDK r26d + O-MVLL 1.8.0。详细工具链、哈希及命令见 [设备验证报告](DEVICE_VALIDATION.md)。

* 正式构建和签名校验通过，最终 APK 已覆盖安装，应用数据保留。
* 223 个类、526 个方法全部转换；526 个方法的代码和异常表逐字节回验通过，最终 APK native 方法集合精确一致。
* 原版和当前 legacy 各完成 3 次预热、20 次有效冷启动，ThisTime 中位数分别为 2010.5 ms 和 2280.5 ms；本批 legacy 相对原版增加 13.43%，不属于新模式相对 legacy 的 10% 门槛判定。
* 5 次前后台切换通过；302.1 秒驻留观察中主 PID 始终一致，没有观察到 Java 崩溃、InternalError、JNI 错误或进程重启。
* 全部原始样本保存在 [本轮 S0 目录](runs/20260912-154155/S0/)，[汇总](runs/20260912-154155/S0/summary.json)可由脚本重新计算。

## 历史 S0 门槛记录（2026-09-12，实施前）

IMPLEMENTATION §6 明确将 **“旧模式功能及性能基线可复现”** 作为从 S0 进入 S1 的条件；ACCEPTANCE §1 要求业务 APK 由目标任务中用户授权确定。目标 APK、规则、mapping、签名和 O-MVLL 配置现已确定并记录哈希。

设备 `192.168.6.118:5555` 已在线并确认 API 27/ARM64。当前测试页面显示未激活、服务器未连接；启动之外的完整业务差分和代表性热点尚未完成。本轮为单份生产随机构建和顺序批次，不替代三组固定 seed 的正式验收。

下表保留当时尚未实施的状态；当前阶段状态以本文开头及 S5_RESULTS 为准。

| 阶段 | 状态与缺口 |
| --- | --- |
| S0 | host、正式 APK、精确方法集合、全方法编码回验、实机启动和短期驻留已有证据；仍缺完整业务差分、代表性热点和三组固定 seed |
| S1 | 未开始；需 S0 门槛通过后改读取层并作语义差分 |
| S2 | 未开始；codec 3、单一 reader 配方、按需读取及两端向量尚无实现 |
| S3 | 未开始；4 行映射、token 目录、编码记录尚无实现 |
| S4 | 未开始；小型 native VM 及并发初始化尚无实现 |
| S5 | 未开始；只有原版/current legacy 的本轮数据，尚无新模式对照，不能判定 10% 门槛 |

后续继续按原方案补齐旧模式业务及热点基线，满足阶段门槛后进入 S1。当前默认保持 legacy，不将本轮基础验证写成全部升级验收通过。
