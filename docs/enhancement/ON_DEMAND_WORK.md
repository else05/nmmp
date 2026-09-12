# 按需解码实施工作记录

本轮从用户已提交的 `14bc959` 开始，保留私有 linker。目标为原 S1–S5 和条件满足后的 P5 组合，不将私有 linker 当成原方案的替代交付。

## 阶段顺序

1. S1 有界读取接口：真实解释器改造前后语义对照、Debug/Release、host sanitizer。
2. S2 codec 3/template 4：单一 reader 配方、逐方法参数、按需代码/异常/payload、固定密钥流缓存；双端向量、完整选中集合回验和实机语义。
3. S3 token/记录/映射：随机目录和独立编码域、边界检查、构建完整性与运行时并发。
4. S4 小型 native VM：Java/C++ 共享程序合同、初始化真实接入、受限执行和失败发布。
5. S5/P5：指定 APK 与私有 linker 组合、功能/结构/启动/热点/内存；未取得的设备或业务证据明确列缺口。默认仍 legacy。

## S0/S1 当前证据

工作输出：`nmm-protect/build/on-demand-20260912/`，与之前 linker 产物隔离。当前唯一设备仍为 API27 ARM64。

既有 S0 的正式 APK/526 方法/启动证据保留。新增独立 Java `app_process` harness 使用真实 ART、JNIWrapper、解释器；参考断言在 Java 中计算，native 指令 fixture 固定，改造前后的源码均从同一提交基线比较。

- Release 原解释器：1075 断言通过。
- Release S1：相同 1075 断言，加截断指令/负越界跳转两项拒绝，共 1077 通过。
- 覆盖整数、64 位值、循环、packed/sparse switch、四种元素宽度与超过 256 字节的 array payload、catch-all、真实空指针异常、对象返回值使用/丢弃和后续 JNI 调用。
- Host ASan/UBSan：旧 codec 和 reader 向量两项通过；LEB128 覆盖合法五字节边界、截断、溢出和错误终止。
- ARM64 Debug 解释器编译与实机 1077 项语义断言通过。

这不是指定业务 APK 全部路径的差分，也不替代三组固定 seed 和正式热点门槛。既有开发工程未提供异常区长度的旧 `vmCode` fixture 暂留显式 legacy 异常适配；正式 `vmExecute` 已传精确长度，经有界 TRIES reader，新模式不得调用旧适配。

读取语义依据原 `READ_ACCESS_AUDIT.md` 已核验的 RE 来源。PC 以 code-unit 偏移表达；六处下一条 move-result-object 预读与当前操作数读取分开；异常读取失败恢复原 Java 异常，不让清理覆盖它。不存在将 360 离线还原格式当作运行时流程的改造。

## S2 当前证据

- codec 3/template 4、显式 `vmDecodeMode=on-demand-v1`（或 `NMMP_VM_DECODE_MODE`）；默认 legacy。
- JSON 单一配方生成 Java/C++ 域合同，Java 构建强制检查同步。每方法 SHA-256 descriptor tag、seed、半字节边界/域描述；64 字节调用独立密钥流窗口按块延迟填充。
- 正式入口排除旧整方法解码，TRIE/payload 均按需读取；一次初始化验证编码区域，调用时仅验证固定大小上下文。
- 独立审查后的补充：参数/seed 一致性校验、实际帧容量、寄存器/对象及引用池逻辑数量检查、invoke 参数数量和堆参数清理、真实数组类别/宽度检查、wrapper 分配失败返回、无效 opcode 拒绝。
- Debug/Release 实机各 1351 断言通过；含记录各字段破坏、READY 后破坏、数组宽度/类型不匹配、寄存器越界。主机 ASan/UBSan 向量通过。
- 指定 APK 526 个方法完整编码/回读一致，签名与原版相同，API27 安装启动并核验当前 PID 主窗口首绘/可见。
- 首次完整构建修正了 codec 3 分支中悬空的 extern 声明；初次失败目录保留。最终阶段产物在 `nmm-protect/build/on-demand-20260912/s2-acceptance/`。
- 本阶段单次启动记录为 2973ms，仅是功能核验样本，不能判定性能门槛；完整交错启动、热点、三组固定 seed 留在 S5。它不能替代完整业务路径验证。

S2 仍直接引用过渡记录；接下来 S3 改 token、目录和编码方法记录，不把当前过渡格式当最终交付。阶段证据在 `runs/20260912-ondemand/S2/`。

## S3 当前证据

- JNI wrapper 已改 token 模块入口，四行随机 opcode 映射、打乱记录和独立 RECORD/MAP/DIRECTORY 编码实际接入；格式见 ON_DEMAND_FORMAT.md。
- 初始化一次完整校验目录、记录与编码区域；root/映射/边界经 mprotect 只读发布，调用只恢复一条记录。条件变量等待、重入永久失败的公共初始化控制器已加入。
- Java 全项目构建通过，新增模块测试通过；独立 Java 固定向量及指定 APK 全 526 方法经 C++ 实际 token/record/reader 逆序读取通过（日志 527 = 526 + 固定向量）。主机 ASan/UBSan 三项通过，覆盖并发、破坏输入、错误 token/帧容量和重入。
- O-MVLL 白名单跟随解释器核心与 token 入口名称更新，正式构建使用仓库配置。
- API27 原签名安装启动通过，当前 PID 4350 主窗口首绘且可见，无 crash marker；单次 ThisTime=2315ms 仅功能样本。阶段产物在 s3-acceptance/。
- S2 直接记录入口仍用于已有语义 fixture；正式生成器不再引用，最终阶段会检查无被链接的过渡执行入口。

接下来 S4 小型 native VM 与绑定初始化状态接入，之后继续 P5/S5；不将 S3 当完整交付。

## S4 当前证据

- 受限 native VM 已实际接入 codec3 root 恢复；独立引导 key、按构建随机 opcode 表、逐条解码，16 寄存器、256 指令和 1024 步上限。JSON 同源生成 Java/C 常量并在构建检查，参考实现只在 Java/测试端。
- root 上下文使用只读页，经初始化控制器发布；绑定证书/包名/APK 核验仍由原宿主实现，临时证书、mask、输入与 VM 寄存器清理。
- 绑定、root、全 DEX 参数准备的初始化回调均不持有状态锁调用 JNI。先准备所有模块再发布参数 READY，之后独立协调待注册类；注册线程合法重入不等待自身，其他激活线程等待注册完成。提前业务调用、注册失败均进入可观察失败路径。
- 主机 ASan/UBSan 8/8，通过 105 Java/C 程序向量、root 1600 次并发调用、实际生成初始化 C 的成功/失败/重入；另一个坏程序编译产物证明永久失败和等待者一致。
- ARM64 Debug/Release 均通过 105 native VM 向量、1600 次 root 激活、1351 语义断言；真实 ART/JNI 回调的 16 线程激活，成功、注册失败、提前业务重入三种场景均通过。
- Release 测试最初因 NDEBUG 关闭 assert 读取而报告 0 向量；已修正测试并重跑，原日志保留但不计通过。旧 Java 断言因参数改为回调 args->context 更新后完整构建通过。
- 正式 O-MVLL 开启的 APK 完成全部 526 方法 Java/native 回验，API27 原签名安装启动、当前 PID 17032 主窗口首绘/可见，无 crash marker。ThisTime=2928ms 是单次功能样本。
- 正式 native 符号存在 vmExecuteToken / nmmpNativeRun / initializeSeed，不含 S2 vmExecuteDemand 或 legacy vmExecute 入口。宿主测试与无 O-MVLL ARM64 harness、正式 O-MVLL APK 均有功能证据。

接下来 P5 stage0 native VM 密钥恢复及 S5 组合性能与验收；API26/完整业务路径缺口不因此消失。

## P5 当前证据

- outer 已接入共享受限 native VM，四个随机编码程序分别恢复 key 的四段。程序检查完整 128-bit loader build ID，独立于 inner root/绑定，不存在循环依赖。旧份额 XOR 仅保留在显式 NMMP_PRIVATE_STAGE0_VM=OFF 对照路径；私有 linker 开启时 stage0 默认 ON。
- 任一程序失败清理全部部分 key；成功后仍走原 ChaCha20-Poly1305 认证，再映射/重定位/构造器/bootstrap。共享引擎没有业务 VM、JNI 或系统调用依赖；outer 不链接 inner 的解释器。
- Python 11 项、主机 ASan/UBSan 4 项通过；ARM64 Debug/Release 各通过 loader、stage0、实际权限/RELRO、并发及绑定/非绑定 bootstrap 共 19 场景，以及 24 Python/C 独立向量。向量 runner 的历史输出标签写 Java/native，这批输入实际来自 Python。
- 正式组合 APK 全部 526 方法 Java/native 回验、API27 原签名启动通过。outer 无旧 key share 符号、无业务解释器、无 inner DT_NEEDED、TLS 或 WX LOAD，inner ELF 严格审计通过。
- 相同 inner SHA/build ID，stage0 OFF/ON 交错各 3 次预热及 20 次有效启动，中位数 2331/2326ms，ON 比 OFF -0.21%；key 恢复中位数 12/52.5us。unpack_us 包含 key_us，不重复相加。此次测量不能替代 S5 的原 legacy/新模式/loader 四组合及三组固定 seed。
- 组合产物：nmm-protect/build/on-demand-20260912/p5-combined/protected-signed.apk，SHA256 8246ee630a9253a003f677ae2b185198ee462b51fe1a2f6702b1106f8e2b85b1。原始证据在 runs/20260912-ondemand/P5/。

S1–S4/P5 功能已经接入，S5 性能验收继续；默认 legacy、私有 linker OFF。API26/完整业务路径尚无通过证据。

P5 生产负向副本：仅损坏第三段 stage0 程序 hash，原签名安装后 loader -6 / JNI_ERR，未出现 READY；随后恢复并验证正常 APK。测试驱动的崩溃等待和外部 logcat 非 UTF-8 问题均保留记录，使用非等待启动和容错解码完成核验。

## S5 验收记录

已构建三组预先固定 seed 的 A/B/C/D（原legacy与当前按需，各自loader OFF/ON）全部组合；每份目标 APK 均保持全部526方法，C/D及无固定seed候选共7份完成真实native reader全方法回验。测试随机入口只影响显式测试配置；生产未设置时仍使用SecureRandom。当前Java 66项、基线48项通过。

按需模式的短方法和大方法短路径已有改进证据，但循环热点明显超过10%门槛，当前legacy路径相对原始基线也有回退；不能宣称性能验收通过。三组正式采样、分阶段对照、受限优化实验与外部缺口统一记录在 [S5_RESULTS.md](S5_RESULTS.md)。当前候选及使用方式见 [USAGE.md](USAGE.md)。

## 最终合同审查及交付边界

补齐了解释器具名解码暂存的显式擦除：固定80字节POD与64字节密钥流窗口独立，正常返回、未捕获异常和读取失败出口清零；不承诺CPU寄存器或编译器副本全零。正式模板、最终JAR与源码精确一致，完整组合候选仍覆盖全部526方法并通过Java/C++回验。

新增退出观察器最初在ART进程退出阶段触发SIGSEGV，该次失败记录保留。相同生产源码关闭hook后正常退出；仅修复测试桥的TLS生命周期，未改变交付运行时。修复后ARM64/API27按需Debug/Release各通过1351语义断言及6408次退出观察，legacy各1338项，进程均正常退出。主机另有16线程首次发布验证。详细证据和精确产物哈希见 [POST_REVIEW.md](POST_REVIEW.md)。

三组冻结版本结果已经汇总，循环热点超过10%门槛；大方法短路径的有限时长数据仅作诊断。最终擦除补齐版本的单seed三项定向复查同样未通过算术/分支门槛，不能由历史三组结果替代当前完整验收。保持默认legacy、私有linker OFF，新模式为实验选项。API26及激活/服务端完整业务仍无验证条件。
