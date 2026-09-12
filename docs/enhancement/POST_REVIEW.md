# 最终审查：显式解码暂存擦除

更新：2026-09-13。源码、主机/API27功能验证及定向对照已完成；完整发布验收未通过。新模式仍为实验选项，默认 legacy、私有 linker OFF。

最终逐项核对 `360_enhance/IMPLEMENTATION.md` 的退出清理合同，发现此前只有 reader 密钥流、seed、记录和 payload 等明确缓冲区清理，解释器的 `inst/ref/vsrc` 及具名操作数暂存没有显式擦除。这是实际修复，不能用调整文档替代。

`VmDecodedState` 集中这些具名暂存，保留原整数宽度和符号；实测固定80字节、8字节对齐，与64字节密钥流窗口分开计算，不随方法长度增长。正常返回和最终异常/读取失败出口统一执行 volatile 字节写零。每个 FETCH 使用独立短期 word，复制必要返回值后清理原word对象，不让复合 FETCH 表达式共用同一可变槽位。捕获异常后继续执行的语义不变；返回值、调用者寄存器帧和 pending exception 不在擦除范围。不存在对 CPU 寄存器、编译器复制或 spill 全面零残留的保证。

测试观察入口仅在 `NMMP_TEST_DECODE_WIPE=ON` 时编译，默认 OFF。最终 JAR、正式模板和源码字节核对一致；最终 outer/inner 的符号检查未发现观察入口、旧/过渡业务执行入口或旧 stage0 key shares。

## 主机与构建证据

- Java 66项通过，失败、错误、跳过均为0；最终模板与JAR内资源一致。
- 真实 OpenJDK/JNI 的 `-O0/-O2` hook 开启及 `-O2` 关闭构建，原语义集各1351项通过。
- 两组退出观察各6408次通过：正常返回、未捕获异常、读取失败、pending exception身份保留、捕获后继续、递归及八线程隔离。入口检查具名成员，退出检查包括padding的整个80字节；主机测试不冒充ART。
- ARM64 Debug/Release 测试工程初次因测试桥缺少 `vm` 头文件搜索路径编译失败；仅补测试target include并保留首次日志，随后按需和legacy两类构建成功。
- 新增强候选与新默认legacy候选均使用原证书、全部526个方法，通过Java全方法回验及精确native方法集合检查；增强候选又通过真实C++ reader的526方法回验。

## 测试观察器的 ART 退出修复

最初 hook 开启的 Release ART 运行打印1351语义断言通过，随后在退出阶段 SIGSEGV，退出码139，该次判失败。崩溃栈经过 `__cxa_thread_finalize`；相同运行时代码关闭观察器后通过1351项并退出0。仅修改测试桥，移除 `thread_local` 帧栈和方法描述符，改用加锁的线程帧表与一次发布的只读描述符；JNI解析不持锁。没有改生产运行时或用 NODELETE 延长库驻留掩盖问题。

修复后主机三种构建各1351项、两组6408次退出观察及两组16线程/1600次首次发布检查均通过且退出0，测试桥目标文件无TLS段或线程析构登记符号。原失败库和日志保留；新实机验证使用独立的 `wipe-device-fixed`。对照及根因证据范围见 `wipe-shutdown-diagnosis/README.md`；未知崩溃PC没有直接符号归属证据。

## 产物

| 产物 | 路径与SHA256 |
| --- | --- |
| 修复后JAR | `nmm-protect/build/libs/vm-protect-2026-09-13-0526.jar`；`28d6f83f8d099fecbcbdfbbd7acc3b41cb7e9b0347260920430d742fa9530ad7` |
| 修复后增强APK | `nmm-protect/build/on-demand-20260912/final-wipe-acceptance/protected-signed.apk`；`0e11e1b2191c53a0b99ea028f4c3a8ed17a610edc722f24a493921287d0cbb96` |
| 默认legacy对照APK | `nmm-protect/build/on-demand-20260912/wipe-legacy-default/protected-signed.apk`；`2f34e190485d57e00041257a6c072e3a2c8df600e2fac020797c9f8bc5023cc5` |

增强APK为未固定测试seed、codec3/template4、按需/token/root native VM/private linker/stage0 VM完整组合。旧候选及三组S5库均未覆盖。三组冻结结果只描述擦除补齐前源码；修复后的定向对照也不能替代完整三组、九场景及API26/完整业务验收。

生产随机配置的证据为：实际构建脚本清除测试seed环境变量且未传 `-Dnmmp.testSeed`，保存的构建日志没有JVM选项注入提示；同环境调用实际 `GeneratorRandom` 得到测试seed为空且实例为 `SecureRandom`。该结论由命令/日志及同环境探针佐证，原生成JVM未额外输出进程内随机入口证明；原始证据为 `wipe-randomness-evidence.json`。

全部输出保留本地，不覆盖外部已部署JAR或脚本。

<!-- DEVICE_RESULTS -->

## 修复后设备验证

旧hook Release在1351项断言完成后退出139；崩溃位于线程退出析构路径，不能计为完整通过。相同runtime的hook OFF在ART通过1351项且exit0。测试桥随后移除两处TLS，使用受锁保护的线程帧表及一次发布的方法描述符；fresh主机回归、并发首次发布和无TLS符号检查通过。修复后ART结果仅取 `wipe-device-fixed`，不覆盖旧失败。证据见 `wipe-shutdown-diagnosis`、`wipe-device`、`wipe-host/tls-free-review`。

ARM64/API27 的按需 Debug/Release 各通过1351语义断言、105条独立 native VM 向量及root测试；两组实际擦除观察各通过6408次退出。legacy Debug/Release 各通过1338语义断言。完整库SHA、命令及输出见 `wipe-device-fixed/summary.json` 和 `commands.jsonl`。

最终设备已恢复本页的增强候选，核对安装SHA、loader build ID与当前主窗口。连续301.0秒观察中主PID保持32474，未出现检查的崩溃标记，结束时主窗口仍首绘可见且resumed。该观察处于未激活状态，不代表完整业务或无泄漏证明。

## 修复后定向性能

同seed1、相同迭代数、两批各5预热/10测量。A为原始legacy OFF；C为冻结的修复前增强private ON；D为修复后增强private ON。这里的C定义仅用于本次修复对照，与S5四组合表不同。所有库均无计数探针。

| 场景 | A中位/P95 ms | 修复前中位/P95 ms | 修复后中位/P95 ms | 修复后/原始 | 修复后/修复前 | 时长规程 |
| --- | --- | --- | --- | --- | --- | --- |
| arithmetic | 1378.93/1487.22 | 3084.29/3097.00 | 3287.74/3311.46 | +138.43% | +6.60% | 满足 |
| branch | 1496.65/1503.44 | 3560.28/3591.82 | 3684.60/3718.79 | +146.19% | +3.49% | 满足 |
| shortCall | 1958.13/1982.50 | 1487.92/1494.49 | 1733.67/1764.34 | -11.46% | +16.52% | 满足 |

这只是单seed、三项热点的修复复查；不能替代修复后完整三组九场景、API26和全部激活业务验收。新模式保持实验选项。原始证据见 [S5目录](runs/20260912-ondemand/S5/)，其中 `wipe-*` 与 `final-wipe-acceptance` 为本次修复产物；其余冻结S5数据仍属于修复前版本。
