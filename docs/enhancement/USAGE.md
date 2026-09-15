# 按需解码与私有 loader 使用

当前保护链固定为按需指令/异常读取、token 化记录、4 行映射、小型 native VM、私有 loader 和 stage0 VM。codec2、直接装载解释器 SO 的构建路径及 XOR 密钥份额恢复已删除。循环热点尚未达到原方案总回退 ≤10% 的门槛；本次清理不代表性能或完整发布验收通过。API26/27 是支持范围，当前实机证据仅有 ARM64/API27。

## 固定执行路径

按需解码、codec3、私有 linker 和 stage0 VM 已经直接固化在构建中，不再提供模式参数。`NMMP_VM_DECODE_MODE`、`-DvmDecodeMode`、`NMMP_PRIVATE_LINKER`、`NMMP_PRIVATE_STAGE0_VM` 及对应 CMake 参数均已删除；设置这些旧环境变量不会改变构建。O-MVLL 插件接入也已删除。仅接受 ARM64，选择 API26 编译目标；运行时只接受 API26/27。全部规则方法保持不变，不支持时明确失败。

当前源码不再包含 legacy 执行模式。如需复查旧版本，应使用历史提交和对应产物；不要把旧模板或生成目录混入当前构建。template5 用于拒绝旧模板，codec3 数据布局保持不变。

## 本轮构建入口

本轮单路径保护器位于 `nmm-protect/build/codec3-only-20260913/delivery/vm-protect.jar`。本轮未覆盖 `D:/Android/SDK_WSL/nmmp/vm-protect.jar` 或 `E:/env-tool/nmmp-wsl.sh`，继续使用这些旧入口不会自动切换到此版本。验收记录见 [兼容路径清理](CODEC3_ONLY.md)。

先将新 JAR 和输入 APK 复制到新的本地工作目录，避免重复写入历史生成目录或混用旧 `tools/vmsrc.zip`。在已配置的 WSL 环境中执行：

```bash
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
unset NMMP_TEST_SEED
export CMAKE_BUILD_PARALLEL_LEVEL=5
java -jar /path/to/new-run/vm-protect.jar apk \
  /path/to/new-run/input.apk \
  /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt \
  /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt
```

生成的 `build/input-protect.apk` 仍需使用已授权本机签名配置对齐、签名。不要把密码或私钥写入报告。修改 native 源码后，通过 `nmm-protect/mksrc/build-src.sh` 同步正式模板，再构建 JAR；仅编译 Android 测试工程不会更新保护器模板。

本轮 APK 输出为 `nmm-protect/build/codec3-only-20260913/delivery/protected-signed.apk`。此前 `on-demand-20260912` 目录及 [S5结果](S5_RESULTS.md)、[最终擦除审查](POST_REVIEW.md) 都是历史产物和历史性能记录，保持不变，不能当作本轮完整性能验收。

## 测试随机参数

仅性能对照使用 `NMMP_TEST_SEED` 或优先级更高的 `-Dnmmp.testSeed=`，严格要求16位ASCII十六进制；空值也报错。它固定 Java 生成器的独立目的随机流，不控制 Python loader 或 O-MVLL，不能保证二进制可复现。两者均未设置时使用 `SecureRandom`；生产构建不要带固定测试参数。

新格式不能与旧模板或记录混用。root、映射、边界描述会在初始化后驻留只读页；指令和异常数据执行时按需读取，每调用最多64字节密钥流缓存、array批次最多256字节，另有固定80字节解释器解码暂存。正常返回与错误出口可靠擦除该暂存，不建立跨调用明文指令/基本块缓存；不承诺返回值、CPU寄存器或编译器副本全部零残留。具体合同见 [格式说明](ON_DEMAND_FORMAT.md)，实现与阶段测试见 [实施记录](ON_DEMAND_WORK.md)。

## 当前源码的签名产物清单构建要求

APK 构建现在必须显式配置独立的 Ed25519 清单签名密钥，缺少配置会在构建开始时失败：

```bash
java -Dnmmp.artifact.privateKey=/absolute/path/artifact-private.pk8 \
  -Dnmmp.artifact.publicKey=/absolute/path/artifact-public.spki \
  -Dnmmp.protectionProfile=enforce \
  -jar /path/to/current/vm-protect.jar apk input.apk convertRules.txt mapping.txt
```

私钥文件为 DER PKCS#8，公钥文件为 DER X.509 SubjectPublicKeyInfo，签名 JDK/provider 必须支持标准 Ed25519。公钥编译入内层；私钥只由构建端读取。此密钥与 APK 原有发布证书绑定是两个独立用途，不替代最终 APK 对齐、签名及系统签名验证。

`assets/nmmp/artifact.sig` 为保留条目：构建会替换输入中同名旧条目，以 STORED 格式写入本次签名清单。初始化复用既有 APK 文件描述符，验签并核对最终 DEX/核心 SO 后才允许解释器激活。缺少清单、配置/身份不符或内容核对失败均不激活。非 APK 的独立 unbound VM fixture 仍使用未配置公钥的默认模板，不能当作可发布的 bound APK 模板。

`GenerateArtifactTestKey.java` 仅用于显式本地验收，不是生产默认密钥生成流程。`build/detection-app-*` 中的 TEST-ONLY 密钥及 APK 均为本地验收材料。完整应用级功能/性能验收尚未因此完成。

## 敏感入口诊断与无 SO 的 APK

通过 `-Dnmmp.sensitiveMethodsFile=<UTF-8 清单>` 指定最多 20 个精确 DEX 方法，详见 [当前 Java 检测](JAVA_DETECTION_CURRENT.md)。只有 `-Dnmmp.diagnostics=true` 才启用日志；构建显式传入 CMake ON/OFF，防止复用缓存时误开启。关闭时 NMMP 日志调用及正文在预处理阶段剔除，loader 失败也不输出日志。开启时 loader、检测、ArtMethod 与旧 ALOG 统一输出到 logcat 和应用私有目录 `/data/data/<package>/check.log`，记录时间、PID、级别、轮次、18 个检查组、环境项执行/缓存/关闭统计、逐项结果及最终判定，并展开已登记方法、SO 可执行段和 GOT 槽位。18 为检查组数量，不代表动态子项总数。默认不启用逐条指令跟踪；手工 LOG_INSTR 也须同时启用诊断才生效。日志文件由应用 UID 拥有，不需要 `WRITE_EXTERNAL_STORAGE`；无法写文件在 logcat 报告 errno，不改变保护判定。检测符号、匹配字符串及 Java 异常信息属于功能数据，不随日志开关删除。

周期环境检测从保护初始化完成开始计时：不足 3 分钟每 5 秒，3～5 分钟每 10 秒，5 分钟起每 30 秒。每轮完成时间开始计算下次间隔，由受保护方法调用提交单个后台任务；无调用时不扫描。线程及 27042/27043 端口每轮重新采样，依赖 Context 的框架类和包 CREATOR 检查保留启动结果。Native 映像完整性每轮后台检查。普通与敏感入口共用周期及采样锁，调用直接使用最近完成的判定，锁忙不拒绝调用。敏感入口仅在抢到本轮任务时在调用线程提取有界调用栈，其余检测与文件日志在附着 JVM 的后台线程执行。启动验证仍同步完成；后台结果约束后续调用，不撤销已开始的调用。线程创建或 JVM 附着失败保留原判定，按周期重试，不在业务线程同步补扫。

API27 ARM64 新增 [ART Runtime Hook 规则](ART_RUNTIME_CURRENT.md)：启动及后台周期比较三个底层 trampoline 的磁盘原码与运行时代码，结合跳板目标归属报告修改信号。enforce 模式下确认修改拒绝后续调用，观察模式只报告；无法检查记录独立不可用状态。系统 libart 文件须可信，不能以这些变化唯一断言 Frida。Policy version 为 7。

不含 native SO 的 APK 现在默认生成 arm64-v8a，不再采用旧的 armeabi-v7a 偏好。APK 实际包含不支持的 ABI 时仍明确失败；不会删除它们后假装是纯 Java APK。

当前示例 APK 的敏感入口全链路记录见 [敏感入口设备验证](SENSITIVE_BINDING_DEVICE.md)。示例不替代业务应用的明确清单和功能验收。
