# on-demand-v1 格式合同

codec 3 / template 5。所有持久化整数使用显式 little-endian，偏移/长度为 u32；Java 数组限制使单模块最大不超过 Integer.MAX_VALUE。仅生成 codec3；template5 删除 codec2 和旧加载构建路径，codec3 字节格式未变化。

## 模块与记录（S3）

每 DEX 一个模块；moduleId 复用该 DEX 的构建 ID，methodId 在整次构建唯一。JNI wrapper 只传模块、随机 u32 token、调用寄存器/标志及实际帧容量。容量是方案伪接口的显式补充，用来拒绝错误记录导致的帧越界。

64 字节 header：magic `VMOD0003` @0；codec @8；template @12；moduleId @16；count @20；directory offset=64 @24；map offset @28；boundary offset @32；boundary bytes @36；data offset @40；total bytes @44；u64 buildId @48；保留零 @56、60。

目录为 count 个 `(token, recordOffset)`，每项 8 字节，按无符号 token 严格递增。记录紧接目录，每条 56 字节，存储顺序随机。随后为 1024 字节映射、编码的边界块、各方法编码指令/异常区；不插入对齐间隙。

记录字段：methodId @0；u64 descriptorTag @4；registersSize @12；insSize @16；code offset/bytes @20/24；tries offset/bytes @28/32；boundary offset/bytes @36/40（相对边界块）；row @44；codec=3 @48；前 52 字节 FNV1a @52。零长度 tries 的偏移也必须位于合法 data 区间。

`methodSeed = mix64(root XOR methodId * 0x9e3779b97f4a7c15 XOR descriptorTag XOR (u64(registersSize) << 32) XOR insSize)`，溢出为 u64。descriptorTag 是最终完整 descriptor 的 SHA-256 前八字节按 LE 解释。row=methodSeed&3。四行独立置换保存「存储编号→本次构建已有 opcode」，NOP=0 固定；只改 FETCH 首字低字节，其他位和 payload 不映射。无效原 opcode 经置换后仍落到解释器拒绝分支。

密钥流继续复用 MethodCodec 的 mix64 / 8 字节位置块。业务代码使用 `(methodSeed,id=0,domain)`；记录使用 `(root,token,RECORD)`；映射与边界使用 `(root,moduleId,MAP/DIRECTORY)`。域编号唯一来源为 reader-formats.json。边界为每 code-unit 半字节：0 padding、1 FETCH、2 OPERAND、3 WIDE、4 PAYLOAD。

构建端完整回读每方法，反向映射后与已有引用重写的结果比较；独立审计从 implementation DEX 重写，并检查最终 APK native 方法集合。C++ 审计通过真实模块/reader 逆序读取全部可读位置及异常字节，padding 仅核对离线零值，不作为可执行目标开放。

## 初始化与驻留数据

一次初始化检查 header、完整编码 blob 的 FNV1a、目录顺序/唯一记录槽、记录 hash/边界、方法 ID 唯一性、数据/边界区无重叠且完整覆盖、四行置换及边界 nibble。初始化临时分配按方法数量增长的校验数组，完成后释放。

root、四行映射和解码边界在匿名 RW 页建立后 mprotect 为 R；不包含明文指令或完整异常区。模块控制状态和 context 指针在原生数据区，使用 acquire/release 发布。等待线程通过条件变量等待；初始化回调在互斥锁外执行；同线程重入进入永久 FAILED 并唤醒等待者，无自动重试。

每调用二分查找 token，只解码一条 56 字节记录并检查 hash/row/偏移/帧容量；不会扫描完整方法。调用独立 reader 保存 64 字节密钥流，不保存明文指令窗口。记录临时缓冲与派生 seed 清理，root/映射/边界长期驻留直至 SO/进程结束。当前不支持模块热卸载。

解释器另有固定80字节的 `VmDecodedState` 保存当前指令字、引用/寄存器索引及具名操作数暂存，正常返回和最终错误出口使用 volatile 字节写零。它不是跨指令复用的明文缓存，不替代64字节密钥流窗口，也不随方法长度增长。每个 FETCH 的短期word独立清理，保留必要的返回值副本；不保证CPU寄存器或优化器复制/spill全面清零。修复及验证边界见 [POST_REVIEW.md](POST_REVIEW.md)。

FNV1a 是一致性检查，不是 MAC；token 目录可枚举，运行中的代码值、root 和密钥流仍可被观察。这里不宣称不可还原或对主动篡改提供密码学完整性。

## 小型 native VM（S4）

合同源为 native-program-format.json，生成 NativeFormats.java / NativeFormats.h。16 字节一条：stored opcode @0，dst/a/b @1/2/3，u64 immediate @4，u16 绝对指令索引 target @12，保留零 @14/15。所有未使用字段必须为零；14 项 semantic→stored 表必须无重复。完整编码程序 FNV1a 校验后逐条检查全部指令（含不可达指令），执行仍逐条解码，不保存完整明文程序。

支持 LOAD_INPUT、CONST、MOV、XOR、AND、OR、ADD、SUB、MUL、SHL、SHR、JULT（u64 小于）、JMP、RETURN；16 个 u64 寄存器，最多 16 输入、256 指令、1024 步，shift 值 0..63。RETURN 固定 r0 值、r1 状态=1，其余状态失败且输出清零。无地址、JNI/FFI/syscall、动态代码或宿主函数调用指令。

独立 programKey 经 MethodCodec(id=0,domain=0x4e564d31) 按位置编码；此 key 与业务 root 无依赖。root 程序校验已核验状态=1、完整 buildId，再将 seedData 与 bindingMask 组合，保持旧参数公式逐位一致。证书与 APK 读取不进入 VM。codec3 native 没有可调用的直接 XOR 参考恢复分支；legacy 的旧公式仅存在于互斥编译分支。

root/各模块状态与全局业务 READY 使用 acquire/release。全局参数初始化阶段严格拒绝同线程业务重入；全部 DEX resolver/map 准备完成后发布 READY，再处理待注册类。注册完成状态独立协调：注册线程在真实 Java/JNI 回调中可访问已完成参数并重入，其他激活线程必须等待待注册集合处理结束。注册失败终止后续业务进入；不取消已运行的调用。状态锁与 pending 队列锁内都不执行 JNI。
