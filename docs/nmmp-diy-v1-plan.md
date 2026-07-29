# NMMP DIY 第一版方案

> 状态：设计已按当前源码复核修订，待实现
> 日期：2026-07-28
> 范围：只覆盖 NMMP 核心转换、运行时解码、字符串池、解释器入口隐藏和必要的一致性保护；不包含后续高级 VM 变形。

## 1. 目标

在不重写 NMMP 解释器语义的前提下，用较小的改动提高静态还原成本：

1. 保留当前每次保护随机生成的 opcode 映射。
2. 对 opcode 重写后的完整指令字节进行每构建、每方法不同的可逆编码。
3. 对异常表和字符串池进行对应编码。
4. 在运行时进入解释器前使用临时缓冲区解码。
5. 将 VM 静态合并到最终 `libnmmp.so`，隐藏清晰的 VM 动态库边界和解释器导出入口。
6. 对编码器/解码器版本、参数和解码结果进行校验，配置不配套时明确失败，不把乱码交给解释器。

本方案的目标是提高自动化分析和批量还原成本，不承诺密码学意义上的不可恢复。算法和密钥最终都在本地工具或产物中，具备足够时间的逆向人员仍然可以恢复。

## 2. 当前核心流程

NMMP 当前的核心不是简单的 opcode 替换，而是：

```text
DEX 方法
  → 提取 MethodImplementation
  → 重写 opcode 和引用索引
  → 生成 C 指令数组与异常表
  → JNI native wrapper
  → vmInterpret
  → C++ DEX 解释器执行
```

当前已经存在一层随机 opcode 映射：

- `RandomInstructionRewriter` 在一次保护任务开始时生成映射。
- `CmakeUtils.generateCSources()` 使用同一个 rewriter 生成匹配的 `DexOpcodes.h`。
- `Dex2c` 和 `JniCodeGenerator` 使用该 rewriter 重写方法指令。

第一版新增的是随机 opcode 外面的“字节编码层”，不替换现有 opcode 机制。

## 3. 最终数据流

### 3.1 方法指令

```text
原始 DEX 指令
  → RandomInstructionRewriter.rewriteInstructions()
  → 随机 opcode 的完整明文字节
  → CODE 域编码
  → static const u1 encodedInsns[]
  → vmExecute()
  → CODE 域解码到独立、对齐的临时缓冲区
  → 明文哈希校验
  → hidden vmInterpret()
```

运行时解码只去掉外层字节编码。解码后的 opcode 仍然是随机 opcode，继续由本次构建生成的随机解释器分发表执行。

### 3.2 异常表

```text
InstructionRewriter.handleTries()
  → 完整异常表明文字节
  → TRIES 域编码
  → static const u1 encodedTries[]
  → vmExecute()
  → 解码到独立临时缓冲区
  → 明文哈希校验
  → 作为 triesHandlers 交给 hidden vmInterpret()
```

### 3.3 字符串池

```text
ModifiedUtf8.encode(string) + 0x00
  → STRING_POOL 域编码
  → static u1 gBaseStrPtr[]
  → resolver_init() 中 pthread_once 原地解码
  → 明文哈希校验
  → 后续解析器持续使用稳定指针
```

字符串池不能使用“每次取字符串时临时解码”的方式，因为当前 `vmMethod.shorty`、native 注册项等会长期保存指向字符串池的 `const char *`。

## 4. 唯一保护上下文

每次 APK、AAB 或 AAR 的 `run()` 调用创建且只创建一个 `ProtectionContext`：

```text
ProtectionContext
├── buildSeed
├── codecVersion
├── nextMethodId
├── nextDexId
└── MethodCodec
```

要求：

- context 是一次保护任务的普通对象，不能使用进程级静态全局状态。
- context 应在每次 `run()` 内、调用 `CmakeUtils.generateCSources()` 之前创建，不能保存为可被下一次 `run()` 复用的字段。
- `buildSeed` 使用 `SecureRandom` 在 context 创建时生成一次；同一次任务中不再重新生成。
- 同一对象必须传给 VM 源码生成和全部 DEX 转换流程。
- `methodId` 在所有 dex 之间全局递增。
- `dexId` 为每个 resolver/string pool 分配一次。
- Java 侧建议使用 `long` 保存 ID，并在输出前检查不超过 `0xffffffffL`。
- 方法没有 `MethodImplementation` 时不分配 `methodId`。
- 编码某个方法时只调用一次 `nextMethodId()`；同一个局部变量同时用于编码和生成 `vmEncodedCode`。

三个入口的调用关系调整为：

```text
run(...) {
    ProtectionContext context = ProtectionContext.create(new SecureRandom())

    CmakeUtils.generateCSources(..., instructionRewriter, context)
    Dex2c.handleAllDex(..., instructionRewriter, context)
}
```

AAR 的 `handleModuleDex()` 使用同样规则。

## 5. Codec 契约

### 5.1 精确参数

| 数据 | ID | Domain | 长度 |
|---|---:|---:|---:|
| 方法指令 | `methodId` | `DOMAIN_CODE` | `encodedInsnsByteSize` 字节 |
| 异常表 | `methodId` | `DOMAIN_TRIES` | `encodedTriesByteSize` 字节 |
| 字符串池 | `dexId` | `DOMAIN_STRING_POOL` | `sizeof(gBaseStrPtr)` 字节 |

key stream 的输入严格定义为：

```text
buildSeed + id + domain + byteIndex
```

`codecVersion` 不参与 key 计算。运行时必须先检查它，再由它选择并确认当前算法；mixer、常量或输入规则变化时必须提升版本。这样版本是算法 ABI，不会出现“文档说版本参与 key、实际公式却未参与”的双重解释。

三个 domain 必须是固定且互不相同的常量。domain 常量、codec 版本和一次任务唯一的 seed 由 Java 生成到 `VmCodecConfig.h`，C/C++ 解码器直接包含该头文件，避免手工复制参数。

### 5.2 对称变换

编码和解码采用相同的 XOR 字节流：

```text
encoded[i] = plain[i]   XOR keyByte(seed, id, domain, i)
plain[i]   = encoded[i] XOR keyByte(seed, id, domain, i)
```

推荐使用基于固定宽度无符号整数的 counter mixer，每 8 字节生成一个 64 位 key block。算法常量属于 codec ABI；算法或常量变化必须提升 `codecVersion`。

推荐的 mixer：

```text
blockIndex = byteIndex >>> 3

z = seed
    XOR (uint64(id) * 0x9e3779b97f4a7c15)
    XOR (uint64(domain) * 0xd6e8feb86659fd93)
    XOR blockIndex

z = (z XOR (z >>> 30)) * 0xbf58476d1ce4e5b9
z = (z XOR (z >>> 27)) * 0x94d049bb133111eb
z =  z XOR (z >>> 31)

keyByte = (z >>> ((byteIndex AND 7) * 8)) AND 0xff
```

跨语言实现要求：

- C/C++ 使用 `uint64_t`、`uint32_t` 和显式无符号运算。
- Java 使用 `long`、`int` 和逻辑右移 `>>>`。
- Java 和 C++ 都只保留对应位宽的低位。
- Java 的 seed 虽由有符号 `long` 承载，但生成头文件时必须按同一 64 位 bit pattern 输出为 16 位十六进制 `UINT64_C(0x...)`，不能依赖有符号十进制格式化。
- ID 和 domain 先约束为 32 位无符号值，再提升到 64 位参与乘法。
- `byteIndex` 和 `blockIndex` 在两端都按 64 位无符号 bit pattern 计算。
- 不使用有符号 `char` 参与 key 计算。
- 必须提供固定 seed/id/domain/input 的跨语言 golden vector。

该算法只是轻量混淆，不作为密码学加密使用。

### 5.3 为什么输出 `u1[]`

编码对象统一为原始字节：

```c
static const u1 encodedInsns[] = { ... };
static const u1 encodedTries[] = { ... };
```

不继续使用编码后的 `u2[]`，从而避免：

- Java `byte[]` 到 C `u2` 的高低字节重新打包错误。
- encoded 常量在编译期受 `u2` 内存布局影响。
- `insnsSize` 究竟表示字节还是 code-unit 的歧义。

运行时用 `malloc()` 分配解码缓冲区；其对齐满足转换为 `u2 *` 后交给解释器使用。当前 NMMP 与目标 Android ABI 本身按 little-endian DEX code-unit 工作，第一版继续这一前提，并在支持的各 ABI 上回归；`u1[]` 解决的是编码层的字节契约，不宣称让现有解释器支持 big-endian 目标。

## 6. 明文一致性校验

错误的 seed、ID、domain、版本或算法如果未被拦截，会把乱码送入 computed-goto 解释器，因此第一版必须 fail-closed。

推荐使用 FNV-1a 32 位校验编码前的完整字节：

```text
hash = 0x811c9dc5
for each unsigned byte:
    hash = hash XOR byte
    hash = hash * 0x01000193  // 保留低 32 位
```

校验范围：

- 指令：`rewriteInstructions()` 返回的完整字节。
- 异常表：`handleTries()` 返回的完整字节。
- 字符串池：全部 MUTF-8 字节以及每个字符串结尾的 `0x00`。

FNV-1a 只用于发现编码器/解码器不配套和意外损坏，不作为防篡改认证。

## 7. 编码态与运行态结构

不要改变解释器当前使用的 `vmCode` 语义。它继续只表示已经可执行的运行态数据：

```c
typedef struct {
    const u2 *insns;
    const u4 insnsSize;
    regptr_t *regs;
    u1 *reg_flags;
    const u1 *triesHandlers;
} vmCode;
```

新增独立的编码态结构，专门作为生成 wrapper 到 `vmExecute()` 的边界：

```c
typedef struct {
    const u1 *encodedInsns;
    u4 encodedInsnsByteSize;

    regptr_t *regs;
    u1 *reg_flags;

    const u1 *encodedTries;
    u4 encodedTriesByteSize;

    u4 methodId;
    u4 plainCodeHash;
    u4 plainTriesHash;
    u2 codecVersion;
} vmEncodedCode;
```

约束：

- 生成的 JNI wrapper 构造 `vmEncodedCode`，不直接构造运行态 `vmCode`。
- 指令长度只用 `encodedInsnsByteSize` 表示字节数；生成端必须断言其非零、为偶数且不超过 `UINT32_MAX`。
- 生成代码使用 `.encodedInsnsByteSize = sizeof(encodedInsns)`，不再进行 `code-unit * 2` 计算。
- 无异常表时，`encodedTries == NULL`、`encodedTriesByteSize == 0`、`plainTriesHash == 0`；有异常表时指针非空，长度非零且不超过 `UINT32_MAX`。
- 有异常表时可使用 `.encodedTriesByteSize = sizeof(encodedTries)`。
- `vmExecute()` 解码成功后才构造局部运行态 `vmCode`，其中 `insnsSize = encodedInsnsByteSize / 2`。

第一版不增加泛化 `flags`，也不增加“明文模式”：

- 当前所有生成方法都使用同一种 codec。
- 暂不支持同一产物中混合多种 codec。
- 现有独立 VM 测试继续直接构造明文 `vmCode` 并调用 `vmInterpret()`，不需要测试 adapter。

分离两个结构可以保留现有解释器和测试的调用契约，避免把编码元数据扩散进解释器主体。

## 8. VM 执行入口

新增入口的固定签名为：

```c
jvalue vmExecute(JNIEnv *env,
                 const vmEncodedCode *encodedCode,
                 const vmResolver *resolver);
```

生成的 JNI wrapper 从：

```text
vmInterpret(env, &code, &dvmResolver)
```

改为调用：

```text
vmExecute(env, &encodedCode, &dvmResolver)
```

`vmExecute()` 负责：

1. 初始化 `jvalue result = {0}`。
2. 在任何解码前校验 `codecVersion == NMMP_CODEC_VERSION`；第一版只有一个算法，不需要运行时多版本分支。
3. 校验指令指针非空，`encodedInsnsByteSize` 非零且为偶数。
4. 校验异常表的指针/长度组合：二者必须同时为空/零或同时有效。
5. 按 `encodedInsnsByteSize` 分配指令缓冲区；有异常表时按 `encodedTriesByteSize` 分配第二个缓冲区。
6. 使用不同 domain 解码指令和异常表，并对解码后的原始字节计算哈希；解码和哈希可以合并为一次遍历。
7. 校验两份明文哈希。
8. 构造局部运行态 `vmCode`，令 `insnsSize = encodedInsnsByteSize / 2`。
9. 调用现有的 hidden `vmInterpret()`。
10. 通过一个统一 cleanup 路径释放所有已经成功分配的缓冲区。
11. 返回解释器结果。

失败处理：

- 分配失败、参数非法、版本不匹配或哈希不匹配时，不进入解释器。
- 使用已经缓存的 `java/lang/InternalError` 抛出明确异常。
- 如果进入失败处理前已经存在 pending JNI exception，不用新的 `InternalError` 覆盖它。
- 任意一步失败都释放此前已分配的部分缓冲区。
- 返回零初始化的 `jvalue`，由 pending JNI exception 决定 Java 层行为。

并发和递归约束：

- 禁止使用全局/static 解码 scratch buffer。
- 禁止解码原数组后再重新编码。
- 每次调用持有独立缓冲区。
- Java 回调再次进入受保护方法时不会覆盖外层解释器正在使用的指令。

`vmInterpret()` 保留现有函数签名和解释器语义，只移除 default visibility 并改为 hidden global；不要收窄为 `static`。因此 `vmExecute()` 可以放在新增的 `Codec.cpp` 或其他翻译单元，不要求搬动大型解释器主体，也不会破坏现有独立 VM 测试。

当前解释器中的：

```c
jvalue retval;
```

同时改为：

```c
jvalue retval = {};
```

避免异常或提前退出路径返回未初始化数据。

## 9. 字符串池

### 9.1 生成端

`ResolverCodeGenerator.generateStringPool()` 保持当前偏移计算：

```text
offset += ModifiedUtf8.encode(string).length + 1
```

随后对完整连续池逐字节编码，包括显式追加的每个 `0x00`。

数组必须由只读改为可写：

```c
static u1 gBaseStrPtr[] = { ... };
```

逐字节 XOR 不改变长度，因此现有所有 `StringId.off` 保持有效。

每个 resolver 还应生成：

```c
static const u4 gStringPoolPlainHash = ...;
static const u4 gStringPoolDexId = ...;
static const u2 gStringPoolCodecVersion = ...;
```

### 9.2 运行端

每个生成的 resolver 文件维护自己的：

```c
static pthread_once_t gStringPoolOnce = PTHREAD_ONCE_INIT;
static bool gStringPoolDecodeOk = false;
```

`pthread_once` 回调没有 `JNIEnv *`，只做以下工作：

1. 先检查 `gStringPoolCodecVersion == NMMP_CODEC_VERSION`，不匹配时不修改池。
2. 使用 `buildSeed + dexId + DOMAIN_STRING_POOL` 原地解码。
3. 对包括每个显式 `0x00` 结尾在内的全部池字节进行哈希校验。
4. 仅在成功时设置 `gStringPoolDecodeOk = true`。

回调不能调用 `ThrowNew()`，异常由拥有 `JNIEnv *` 的 `resolver_init()` 传播。`resolver_init()` 改为返回 `bool`，其入口契约为：

```c
static bool resolver_init(JNIEnv *env) {
    int rc = pthread_once(&gStringPoolOnce, decode_string_pool_once);
    if (rc != 0 || !gStringPoolDecodeOk) {
        (*env)->ThrowNew(env, gVm.exInternalError,
                         "NMMP string pool decode failed");
        return false;
    }

    /* static cache/ready arrays rely on zero initialization */
    return true;
}
```

`resolver_init()` 第一条有效操作必须是 `pthread_once()`；它必须位于当前几个 `sizeof(...) == 0` 的提前返回之前。推荐直接删除现有 cache `memset` 和对应的零长度提前返回：文件级 static cache/ready 数组已由 C 运行时零初始化，重复 setup 时再次清零反而会丢失已发布项并泄漏 global ref。修改后的 `resolver_init()` 应保持幂等。

生成的每个 dex setup 使用：

```c
if (!resolver_init(env)) {
    return;
}
```

`JNI_OnLoad` 必须在 `cacheInitial()` 后以及每个 setup 调用后检查异常：

```c
cacheInitial(env);
if ((*env)->ExceptionCheck(env)) {
    return JNI_ERR;
}

setupN(env);
if ((*env)->ExceptionCheck(env)) {
    return JNI_ERR;
}
```

这样 `pthread_once` 调用失败、池哈希失败、缓存初始化失败和 native 注册失败都会终止加载，不会继续使用未初始化数据。

解码成功后不重新编码，因为当前 resolver 会保存稳定字符串指针。

## 10. Resolver 并发发布

字符串池的一次性解码先于任何 resolver 使用。字段、方法和字符串常量缓存采用确定的一套发布方案，不在“mutex 或 CAS”之间留实现歧义。

每个 resolver 生成独立的 ready 数组和一个只用于发布的 mutex；计数为零时不生成零长数组，或生成一元素占位但不访问：

```c
static u1 gFieldReady[FIELD_COUNT];
static u1 gMethodReady[METHOD_COUNT];
static u1 gStringReady[STRING_COUNT];
static pthread_mutex_t gResolverPublishMutex = PTHREAD_MUTEX_INITIALIZER;
```

统一发布流程：

1. 读取方先用 `__atomic_load_n(&ready[index], __ATOMIC_ACQUIRE)`；已就绪时直接读取缓存。
2. 未就绪时在锁外执行 `FindClass`、`GetMethodID`、`GetFieldID`、`NewStringUTF`、`NewGlobalRef` 等 JNI 工作。
3. JNI 解析失败或已有 pending exception 时，清理本线程候选对象并返回，不发布 ready。
4. 解析成功后进入 `gResolverPublishMutex` 的短临界区，并再次检查 ready。
5. 获胜线程先复制缓存项的全部字段，最后用 `__atomic_store_n(&ready[index], 1, __ATOMIC_RELEASE)` 发布。
6. 失败线程不覆盖已发布缓存，并清理自己的候选资源。

字段和方法缓存不需要候选 global ref；竞争失败线程只清理解析过程中持有的 local ref。字符串常量按以下规则处理：

1. 锁外创建 local `jstring` 和候选 `GlobalRef`。
2. 锁内二次检查；未发布时保存候选 global ref，再以 release store 设置 ready。
3. 竞争失败时对自己的候选 global ref 调用 `DeleteGlobalRef()`。
4. 无论胜负都清理自己的 local `jstring`。
5. 解锁后对最终已发布的 global ref 调用 `NewLocalRef()`，把 local ref 返回给解释器。

mutex 只保护“二次检查 + 完整复制 + ready 发布”，绝不跨 JNI 解析调用持有，避免 Java/JNI 重入造成死锁。该调整不参与 codec key 计算，但属于第一版的 resolver 正确性修复；应作为独立提交完整实现。如果不能完整实现，就整体延后，不能只加 ready 标志而保留非原子发布。

## 11. VM 入口隐藏

### 11.1 链接结构

当前：

```text
libnmmp.so → libnmmvm.so
```

第一版改为：

```text
nmmvm STATIC/OBJECT
      ↓
libnmmp.so
```

要求：

- 只把生产模板中的 `nmmvm` 从 `SHARED` 改为 `STATIC` 或 `OBJECT`。
- 静态对象启用 PIC。
- 最终 `nmmp` 使用 C++ linker。
- VM target 和最终 target 使用 target 级 hidden visibility，不只依赖 Release 全局 flags。
- 删除 `vmInterpret`、`gVm`、`cacheInitial`、`getCacheClass` 等非 JNI 必需符号上的显式 default visibility。
- `gVm`、`cacheInitial`、`getCacheClass` 等仍被跨翻译单元使用的符号保持 global hidden，不能为了隐藏而改成 `static`。
- `JNI_OnLoad` 继续通过 `JNIEXPORT` 导出。
- `BuildNativeLib` 删除 `VM_NAME` 及 `libnmmvm.so` 查找/打包分支，只处理最终 `libnmmp.so`。
- `nmmvm/nmmvm/src/main/cpp/CMakeLists.txt` 属于独立 VM 测试工程，可继续生成测试用 `SHARED` 目标；静态合并规则写在生产用的 `nmm-protect/mksrc` CMake overlay 中。

### 11.2 内部符号

- `vmInterpret`：保留现有 global 调用契约，设置为 hidden，不进入动态符号表；独立 VM 测试仍可直接链接调用。
- `vmExecute`：供生成代码跨翻译单元调用，设置为 hidden，不进入动态符号表。
- 字符串池需要的共享 decode helper：跨翻译单元时可以是 hidden global，不能声明为 `static`。
- `vmExecute` 和供生成 `.c` 文件调用的 codec helper 声明必须放在 `extern "C"` 保护中，避免 `Codec.cpp` 的 C++ name mangling 导致链接失败。

生产构建经过 strip 后检查动态符号表；`JNI_OnLoad` 是预期保留的 JNI 导出，`vmInterpret`、`vmExecute` 和 decode helper 均不得作为动态符号出现。隐藏入口只能消除明显的导出符号和独立动态库边界，不能阻止静态反汇编发现解释器函数。

## 12. VM 模板与 seed 注入

生产保护流程使用 `vmsrc.zip`，而不是直接使用开发目录中的 VM 源码。因此必须建立明确的模板版本契约。

### 12.1 生成配置

VM 源码树必须提交一个可供独立测试构建使用的配置头：

```text
vm/include/VmCodecConfig.h
```

该头包含模板版本以及固定的测试/default 配置，并用 `#ifndef` 允许生产值覆盖，例如：

```c
#define NMMP_CODEC_TEMPLATE_VERSION 1

#ifndef NMMP_CODEC_VERSION
#define NMMP_CODEC_VERSION 1
#endif

#ifndef NMMP_BUILD_SEED
#define NMMP_BUILD_SEED UINT64_C(/* 固定测试 seed */)
#endif

/* 三个 domain 同样提供固定测试默认值 */
```

`nmm-protect/mksrc/build-src.sh` 将该头和 codec 源文件一起打入模板。生产流程中，`CmakeUtils.generateCSources()` 解压模板、检查模板标记后，用本次 `ProtectionContext` 的 seed、固定 domain 和 codec 版本覆盖解压目录中的同一路径：

```c
#define NMMP_CODEC_TEMPLATE_VERSION 1
#define NMMP_CODEC_VERSION ...
#define NMMP_BUILD_SEED UINT64_C(...)
#define NMMP_DOMAIN_CODE ...
#define NMMP_DOMAIN_TRIES ...
#define NMMP_DOMAIN_STRING_POOL ...
```

选择 `vm/include` 是因为 VM 子目录可以稳定包含自己的头文件；同时，提交默认配置可保证 `nmmvm` 独立测试工程不会因为缺少生产生成步骤而无法编译。`Codec.cpp` 必须同时加入独立 VM CMake 和生产 CMake overlay。

### 12.2 模板版本

VM 模板提供固定标记，例如：

```c
#define NMMP_CODEC_TEMPLATE_VERSION 1
```

生成器解压后必须检查：

- 标记文件存在。
- 模板版本与 Java 生成器期望版本一致。
- codec 所需头文件和源文件存在。

任何一项不满足就停止，不允许继续使用旧 decoder。

当前外部 `tools/vmsrc.zip` 存在时不会被内部资源覆盖，这是最可能导致“新编码端 + 旧解码端”的路径。发现模板版本不匹配时必须 fail-closed 并报告所用文件及期望版本，不能自动覆盖，因为该文件可能是用户定制模板。若希望自动管理缓存，应使用带模板版本的独立缓存文件名，而不是原地改写既有 `tools/vmsrc.zip`。

### 12.3 源码同步

源码真源和生产 overlay 必须分开描述：

- VM C/C++ 源码真源：`nmmvm/nmmvm/src/main/cpp/`。
- 生产根 CMake overlay：`nmm-protect/mksrc/CMakeLists.txt`。
- 生产 VM CMake overlay：`nmm-protect/mksrc/vm/CMakeLists.txt`。
- 生产资源：`nmm-protect/apkprotect/src/main/resources/vmsrc.zip`。

`nmm-protect/mksrc/build-src.sh` 从 VM 源码真源复制 `vm/`、`cutils/`、`ConstantPool.c` 和 `ConstantPool.h`，再覆盖生产 CMake 文件并生成：

```text
nmm-protect/apkprotect/src/main/resources/vmsrc.zip
```

因此 codec、`vm.h`、`vmInterpret` 和默认 `VmCodecConfig.h` 的改动先提交到 `nmmvm/nmmvm/src/main/cpp/`；静态合并、最终共享库和生成源清单等生产差异提交到两个 `mksrc` CMake overlay；最后运行脚本重建资源 zip。

## 13. 主要代码改动点

Java：

- 新增 `ProtectionContext`，每次 `run()` 内用 `SecureRandom` 创建一次。
- 新增 `MethodCodec`/`CodecSpec`。
- `ApkProtect`、`AabProtect`、`AarProtect` 创建并传递同一个 context。
- `CmakeUtils.generateCSources()` 生成 codec 配置并校验模板版本。
- `Dex2c` 的各级入口继续传递 context。
- `JniCodeGenerator`：
  - 在确认 implementation 非空后分配一次 `methodId`。
  - 对重写后的完整指令字节编码。
  - 对完整异常表编码。
  - 输出 `u1[]`、字节长度、ID、版本和明文哈希。
  - 断言指令字节长度非零且为偶数。
  - 构造 `vmEncodedCode` 并调用 `vmExecute()`。
- `ResolverCodeGenerator`：
  - 分配一次 `dexId`。
  - 编码完整 MUTF-8 字符串池。
  - 生成一次性解码和哈希校验代码。
- `GlobalDexConfig`：
  - `cacheInitial()` 后和每个 dex setup 后检查 JNI exception。

C/C++：

- `vm.h` 保留现有运行态 `vmCode`，新增 `vmEncodedCode` 和 `vmExecute()` 声明。
- `InterpC-portable.cpp`：
  - 保留 hidden global `vmInterpret()`，不收窄为 `static`。
  - 初始化 `jvalue`。
- 新增 `Codec.cpp` 或等价 codec helper，实现 hidden `vmExecute()`，保证 Java/C++ 算法完全一致。
- `GlobalCache` 等删除不必要的 default visibility。
- resolver 缓存按独立 ready 数组、acquire/release 和短发布 mutex 的规则完整实现。

构建和打包：

- 在 `nmmvm/nmmvm/src/main/cpp/` 提交 codec 源码、头文件、默认测试配置并加入独立测试 CMake。
- `nmm-protect/mksrc/vm/CMakeLists.txt` 将生产 VM 改为静态目标并加入 codec 源文件。
- `nmm-protect/mksrc/CMakeLists.txt` 只生成最终共享库，并对 C/C++ 同时启用 target 级 hidden visibility。
- `BuildNativeLib` 删除 `VM_NAME` 相关逻辑，只收集 `libnmmp.so`。
- 用 `nmm-protect/mksrc/build-src.sh` 重新生成并更新资源 `vmsrc.zip`。

测试执行：

- 当前 `nmm-protect/build.gradle` 会禁用 `apkprotect` 的 `Test` 任务；实现测试前必须删除/收窄该禁用规则，或新增一个明确保持 enabled 的 codec 测试任务。
- Java 与 C++ 各自执行同一组 golden vectors，不能只在一侧自测 encode/decode。

## 14. 推荐实施顺序

### 阶段 1：先固定 codec 契约

1. 实现 Java `CodecSpec` 和 `MethodCodec`。
2. 实现 C/C++ 同算法 helper。
3. 提交独立 VM 默认 `VmCodecConfig.h`，并让生产生成器覆盖同一路径。
4. 启用实际会执行的 Java/C++ codec 测试任务。
5. 添加两端共享的固定 golden vectors。
6. 验证不同长度下 `decode(encode(data)) == data`。

通过标准：Java 和 C++ 对同一输入生成完全一致的 key stream，独立 VM 构建不依赖生产生成步骤。

### 阶段 2：方法指令和异常表

1. 引入 `ProtectionContext`。
2. 贯通 APK/AAB/AAR 和所有 DEX。
3. 修改 `JniCodeGenerator` 输出 `vmEncodedCode`。
4. 实现 `vmExecute()`。
5. 增加版本、长度和哈希失败路径。

通过标准：普通方法、分支、switch、fill-array、异常方法行为与修改前一致。

### 阶段 3：字符串池

1. 编码完整 MUTF-8 池。
2. 改成可写数组。
3. 在 `resolver_init()` 最前面一次性解码。
4. 增加 pool hash 和失败传播。

通过标准：空字符串、中文、emoji、Java `\u0000`、方法签名和 native 注册均正常。

### 阶段 4：隐藏入口

1. VM 改静态链接。
2. 移除显式默认可见性。
3. 调整最终链接语言和 PIC。
4. 删除打包侧 `libnmmvm.so` 依赖。

通过标准：产物只有 `libnmmp.so`，且 strip 后的动态符号表没有 `vmExecute`/`vmInterpret`。

### 阶段 5：并发与模板收口

1. 以独立提交完整实现 resolver ready 数组、短发布 mutex 和候选引用清理。
2. 添加模板版本检查。
3. 从 VM 源码真源重新生成资源 zip，并核对两个生产 CMake overlay。
4. 执行完整 ABI 和回归测试。

resolver 并发修复必须整组落地；如果当前无法覆盖字段、方法和字符串三条路径，就从第一版提交中整体延后，不能提交一半的发布协议。

## 15. 验证矩阵

### 15.1 Codec 单元测试

- 通用 codec helper 覆盖长度 0、1、2、3、7、8、9、奇数和较大数组。
- `vmExecute()` 单独验证指令长度 0 和奇数会在解码前被拒绝，合法偶数字节长度可正确换算为 code-unit。
- 全零、全 `0xff`、递增字节、随机字节。
- 相同 seed、不同 methodId 的输出不同。
- 相同 seed/id、不同 domain 的输出不同。
- 错误 codec 版本在解码前被拒绝；错误 seed/id/domain 或损坏数据在哈希处失败。
- 所有失败路径均验证没有进入 `vmInterpret()`。
- Java 测试和 C++ 测试目标使用同一组 golden vector，输出完全一致。
- 测试任务在实际 CI/Gradle 调用中为 enabled，而不是只存在源码。

### 15.2 解释器语义测试

- 无返回值及所有基础返回类型。
- 静态/实例方法。
- 多参数和宽寄存器。
- 条件跳转、循环。
- packed-switch、sparse-switch。
- fill-array-data。
- 对象创建、字段访问、方法调用。
- try/catch、多个 handler、catch-all。
- 解释器内部 JNI 调用产生 pending exception。
- 递归调用受保护方法。

### 15.3 字符串池测试

- 空字符串。
- ASCII。
- 中文。
- emoji/代理对。
- Java 字符串中的 `\u0000`。
- 类名、字段名、方法名、shorty、signature。
- 多 dex 各自独立字符串池。
- 重复调用 setup 不发生二次 XOR。
- 重复调用 setup 不清空已经发布的 resolver cache，也不泄漏 global ref。

### 15.4 并发测试

- 多线程同时首次解析同一字段。
- 多线程同时首次解析同一方法。
- 多线程同时首次加载同一字符串常量。
- 重复竞争字符串常量时，候选 global ref 无泄漏，返回值均为有效 local ref。
- 受保护方法调用 Java 后重入另一受保护方法。

### 15.5 产物测试

- 至少验证 `arm64-v8a`、`armeabi-v7a`，条件允许时再验证 `x86_64`。
- APK/AAB/AAR 三条入口均使用同一 context 规则。
- `readelf -d` 不再依赖 `libnmmvm.so`。
- 对 strip 后的最终库执行 `readelf --dyn-syms`，不暴露 `vmExecute`、`vmInterpret` 和 decode helper。
- 故意使用错误 seed、ID、domain、版本或哈希时明确失败，且不进入解释器、不发生随机 native crash。
- 独立 `nmmvm` 测试构建能使用提交的默认 `VmCodecConfig.h`。
- 资源 zip 包含当前模板标记、codec 源文件和配置头。
- 使用旧资源或外部定制 `tools/vmsrc.zip` 时构建阶段直接拒绝，并且不自动覆盖该文件。

## 16. 验收条件

第一版只有同时满足以下条件才算完成：

1. 解码结果与编码前的重写指令逐字节一致。
2. 异常表解码结果逐字节一致。
3. 字符串池解码后所有原始 offset 保持有效。
4. Java/C++ codec golden vectors 全部通过。
5. 错误 seed、ID、domain、版本和损坏数据不会进入解释器。
6. 编码态只使用字节长度，运行态 `insnsSize` 只在校验后通过除以 2 得出。
7. 原有 NMMP 解释器回归测试继续直接使用运行态 `vmCode` 并全部通过。
8. 递归和多线程执行没有共享解码缓冲区、半发布缓存或 JNI 引用泄漏。
9. 独立 VM 配置、生产生成配置和资源模板版本契约均有实际执行的测试。
10. 最终产物不再包含独立 `libnmmvm.so`。
11. `vmInterpret`、`vmExecute` 和 decode helper 不在动态符号表中。
12. APK、AAB、AAR 的真实样例均可正常安装、加载和执行。

## 17. 已知代价

- 每次受保护方法调用增加指令和异常表解码。
- 每次调用通常增加一到两次临时内存分配。
- 字符串池在 `JNI_OnLoad` 时增加一次解码和校验。
- VM 静态合并后 `libnmmp.so` 体积增加，但总 APK 中不再有独立 VM so。
- 明文指令在方法执行期间存在于进程内存。
- 字符串池解码后会长期以明文保留在可写内存中。

第一版优先保证正确性、递归安全和线程安全，不引入共享缓存、线程本地复用或复杂内存池。性能优化应在基准测试后单独设计。

## 18. 第一版明确不做

- 多套解释器语义。
- 每个方法生成不同 handler 实现。
- 指令融合或 super-instruction。
- 控制流平坦化。
- 动态 opcode 映射。
- JIT/运行时生成解释器。
- 解码后重新编码共享原数组。
- 全局或线程共享解码缓存。
- 反调试、反注入、完整性认证。
- 密钥远程下发。
- 构建系统通用安全加固。

这些内容都不是验证第一版编码/解码闭环所必需的。

## 19. Review 结论

该方案可以适配当前 NMMP 核心，不需要修改现有 DEX 指令语义或重写整个解释器。

不会造成后续解码不配套的前提是：

1. 每次 `run()` 新建且只使用一个 `ProtectionContext`，seed 只生成一次。
2. 编码和解码都以原始 `u1` 字节为单位。
3. 每个数据块只分配并保存一次 ID。
4. code、tries、string pool 使用固定且不同的 domain。
5. Java/C++ 使用完全相同的无符号运算；codec 版本在解码前检查，但不混入 key。
6. 字符串池在任何使用前只解码一次。
7. 模板版本不匹配时停止构建。
8. 运行时哈希不匹配时停止执行，不进入解释器。
9. 编码态 `vmEncodedCode` 与解释器运行态 `vmCode` 保持分离。

满足这些约束后，外层编码对当前解释器是透明的：hidden `vmInterpret()` 接收到的运行态 `vmCode` 仍包含原来 `rewriteInstructions()` 和 `handleTries()` 生成的数据。
