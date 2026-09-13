# Codec3 单路径清理

日期：2026-09-13。基线 `e7e1e15`。依据用户明确要求删除旧兼容代码，替代先前“默认legacy、私有linker OFF”的构建策略。

## 删除与保留

- 删除 Java 生成器的 codec2 整方法编码、`vmEncodedCode` 记录、`vmExecute` 调用及相关分支；native `Codec.cpp` 与对应声明、构建源项、O-MVLL 白名单一并删除。
- root 参数恢复只走受限 native VM，移除 codec2 的直接seed/XOR激活分支。字符串池等仍使用公共 `MethodCodec`/`VmCodec` 编码算法，这些不是另一条 codec2 执行路径。
- 正式 CMake 固定构建 outer/inner；移除直接将解释器链接到可加载 SO 的替代分支。系统仍负责加载外层 SO，外层再私有加载内层，这是完整新链路所必需。
- stage0 固定通过四段 native VM 程序恢复密钥，删除旧 key share XOR 生成及运行时代码。
- S2 直接记录入口只用于错误输入和JNI语义测试，已移到 `src/test/semantic/DemandFixture.*`；正式模板和运行时只保留 token 模块入口。
- 删除必须依赖“私有linker OFF”的旧分配计数探针及其驱动。历史实验日志及基线提交保留，当前仍保留正式热点/启动采集器。
- 共享指令handler、JNI桥、签名/包名校验、边界检查、显式解码暂存擦除继续保留。

## 配置与模板

不设置开关时直接生成 `on-demand-v1 + private linker + stage0 VM`。旧参数只有输入校验用途：`on-demand-v1`/`ON` 可接受，`legacy`/`OFF` 明确失败，不能切换回旧路径。

数据格式仍为codec3；模板升级为5以拒绝旧源码包混用。开发测试使用build ID 0的固定root程序，由 `GenerateDefaultRoot.java` 可重现；正式APK生成器每次覆盖它并独立生成程序。

## 验证

测试及产物输出位于 `nmm-protect/build/codec3-only-20260913/`。Java最终构建66项、native Debug/Release/ASan+UBSan各5项、Python loader 12项及ARM64/API27 Debug/Release语义已通过。两组ART各1351断言、105 native VM向量、1600次root激活和6408次清零观察，所有进程退出0。

正式APK的526个方法通过Java记录及native逆序读取回验，选中方法集合与前一版完全相同。模板与源码匹配，产物中旧执行入口、旧key share和测试探针符号均不存在。默认构建和显式新参数成功，legacy、两个OFF和模板4被拒绝。

## 新产物与设备结果

稳定交付路径位于上述输出目录的 `delivery/`，未覆盖外部部署的JAR或启动脚本。

| 项目 | 结果 |
| --- | --- |
| JAR | `delivery/vm-protect.jar` |
| JAR SHA256 | `9f902ab85ec9eda9fb698076cf952d9d3f6195a4cff91b3470b8144704006784` |
| APK | `delivery/protected-signed.apk` |
| APK SHA256 | `9bfb65a6b0cb5b082d47c948c78acb0bb1dc36fd9612fec40591103e845680e0` |
| APK大小 | 9,220,037字节，约8.79MiB |
| 启动 | 单次功能检查ThisTime 3195ms，不是性能对照 |
| 内存 | 一分钟未激活页面驻留后TOTAL PSS 57,872KB，约56.5MiB |

已使用原签名覆盖安装并保留数据。API27 ARM64设备上观察一分钟，四次采样PID均为29527，主窗口保持可见，采集范围内未发现崩溃标记。该观察不代表完整业务、泄漏或长期稳定性验收。

本轮[原始证据](runs/20260913-codec3-only/verification.json)、[产物校验](runs/20260913-codec3-only/result.json)、[设备驻留](runs/20260913-codec3-only/observation/summary.json)及文件SHA256清单位于 `runs/20260913-codec3-only/`，APK/JAR留在本地构建目录。

本次不以旧S5测量代替新产物性能验收，也不声称循环热点已优化到10%以内。API26设备与激活/服务端完整业务仍缺验证条件。

历史 `S5_RESULTS.md`、`POST_REVIEW.md` 和 `runs/20260912-ondemand/` 保留原版本语义；当前使用方式以 `USAGE.md` 为准。
