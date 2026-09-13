# 按需解码与私有 loader 使用

当前保护链固定为按需指令/异常读取、token 化记录、4 行映射、小型 native VM、私有 loader 和 stage0 VM。codec2、直接装载解释器 SO 的构建路径及 XOR 密钥份额恢复已删除。循环热点尚未达到原方案总回退 ≤10% 的门槛；本次清理不代表性能或完整发布验收通过。API26/27 是支持范围，当前实机证据仅有 ARM64/API27。

## 模式

| 原开关 | 不设置时 | 显式设置 |
| --- | --- | --- |
| `NMMP_VM_DECODE_MODE` | 固定 `on-demand-v1`，codec3/template5 | 只接受 `on-demand-v1`；`legacy` 报错 |
| `NMMP_PRIVATE_LINKER` | 固定开启 | 只接受 `ON`；`OFF` 报错 |
| `NMMP_PRIVATE_STAGE0_VM` | 固定开启 | 只接受 `ON`；`OFF` 报错 |

上述参数仅保留输入校验，不能选择另一条执行路径。Java 属性 `-DvmDecodeMode` 优先于模式环境变量，同样仅接受 `on-demand-v1`。CMake 的旧 OFF/legacy 参数也会被拒绝。仅接受 ARM64，选择 API26 编译目标；运行时只接受 API26/27。全部规则方法保持不变，不支持时明确失败。

当前源码不再包含 legacy 执行模式。如需复查旧版本，应使用历史提交和对应产物；不要把旧模板或生成目录混入当前构建。template5 用于拒绝旧模板，codec3 数据布局保持不变。

## 本轮构建入口

本轮单路径保护器位于 `nmm-protect/build/codec3-only-20260913/delivery/vm-protect.jar`。本轮未覆盖 `D:/Android/SDK_WSL/nmmp/vm-protect.jar` 或 `E:/env-tool/nmmp-wsl.sh`，继续使用这些旧入口不会自动切换到此版本。验收记录见 [兼容路径清理](CODEC3_ONLY.md)。

先将新 JAR 和输入 APK 复制到新的本地工作目录，避免重复写入历史生成目录或混用旧 `tools/vmsrc.zip`。在已配置的 WSL 环境中执行：

```bash
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
unset NMMP_TEST_SEED NMMP_VM_DECODE_MODE NMMP_PRIVATE_LINKER NMMP_PRIVATE_STAGE0_VM
export OMVLL_CONFIG=/mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/scripts/wsl/omvll-config.py
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
