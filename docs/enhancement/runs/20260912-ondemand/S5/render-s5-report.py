from pathlib import Path
import json
import subprocess
import sys

w = Path(__file__).resolve().parent
root = w.parents[2]
subprocess.run([sys.executable, '-B', str(root / 'nmm-protect/scripts/summarize-enhancement.py'), str(w), str(w / 's5-summary.json')], check=True)
report = json.loads((w / 's5-summary.json').read_text(encoding='utf-8'))
lines = ['# S5 组合验收结果', '',
    '功能已接入，性能验收未通过。新模式保留为显式实验选项；默认 legacy、私有 linker OFF。API26 与激活/服务端业务路径仍缺实机证据。', '',
    '本报告由原始 ledger 重算；冻结的三组各场景及独立启动复核的样本采集状态：' + ('已完成。' if report['measurementComplete'] else '**进行中。**'), '',
    '最终合同审查发现的解释器具名解码暂存擦除缺口已补齐，详见 [POST_REVIEW.md](POST_REVIEW.md)。以下三组库保持冻结，属于擦除补齐前版本；不得把这些数字当成修复后JAR/APK的完整性能验收。修复后的构建、功能回归和定向性能对照另行记录。', '',
    '## 对照与产物', '',
    'A=原提交 14bc959 legacy/loader OFF；B=同基线 legacy/loader ON；C=当前按需/native VM/loader OFF；D=当前按需/native VM/loader ON、stage0 VM ON。每组均使用同一个授权 APK、规则、mapping、原证书及全部 526 个方法。三组A/B使用基线快照JAR，SHA256 `9033d186fe875858552177856c126f51b34264d0d52ca579138fd584be497747`；C/D使用增强JAR，SHA256 `28c13bbb3952d69640ad5bae626f0f5b7d04346e701e43dded0efd5e3734b469`。两者文件名均为 `vm-protect-2026-09-13-0136.jar`，所在目录不同。', '',
    '三组预先固定 seed 为 `0123456789abcdef`、`fedcba9876543210`、`6a09e667f3bcc909`。基线只补充 Java 测试随机入口，121 个 native/template 文件与 14bc959 逐字节相同；共享 context/opcode/resolver/JNI 随机流三组一致。Python loader 和 O-MVLL 随机性未固定，不声称二进制可复现。', '',
    '实际设备 `192.168.6.118:5555`，ARM64/API27；正式构建为 NDK26.3/Clang17、API26、Release、O-MVLL1.8 与同一仓库配置。设备电池报告 present=false、温度0，不能作为有效温度。设备时钟为2020年，与主机不同；当前日志用设备 epoch 起点过滤，以免 PID 复用命中历史 crash buffer。', '',
    '## 启动', '',
    '每变体每 seed 3 次预热、20 次有效进程冷启动，按轮交错正反顺序覆盖安装。保留数据；检查 force-stop 后 PID 消失、当前主窗口首绘/可见、resumed、日志和安装 hash。ThisTime 用于门槛；窗口观察耗时只作上界。所有有效样本保留，不按速度筛选。', '',
    '| seed | A 中位/P95 ms | B 中位/P95 ms | C 中位/P95 ms | D 中位/P95 ms | B/A | D/C | D/A |',
    '| --- | --- | --- | --- | --- | --- | --- | --- |']
for name, seed in report['seeds'].items():
    s = seed['startup']
    if not s['complete']:
        lines.append('| ' + name + ' | 尚未完成 | | | | | | |')
        continue
    v = s['variants']
    cells = [name] + [f"{v[n]['median']:g}/{v[n]['p95']:g}" for n in 'ABCD']
    cells += [f"{v[n]['percentVs' + ref]:+.2f}%" for n, ref in [('B','A'), ('D','C'), ('D','A')]]
    lines.append('| ' + ' | '.join(cells) + ' |')
lines += ['', 'seed1首批历史采集尚未记录device epoch和预期loader build ID，仅检查该PID的READY。独立复测使用epoch边界及build ID核验；未改写旧样本。', '', '### 同 seed 独立噪声复核', '',
    '首次三组只是点估计未超限，尚不能据此宣告启动验收通过。每轮配对回退中位数为+0.722%、+2.225%、+1.751%，仅用于诊断。seed3快/慢样本分布A为10/10、D为9/11，边际中位数对时间分段敏感；逐轮配对排除的敏感性计算落在−1.03%～17.95%，不用于实际删样本。按规程追加同seed独立批次，续采不算独立批次。', '',
    '| seed | A 中位/P95 ms | B 中位/P95 ms | C 中位/P95 ms | D 中位/P95 ms | B/A | D/C | D/A |',
    '| --- | --- | --- | --- | --- | --- | --- | --- |']
for name, seed in report['seeds'].items():
    s = seed['startupIndependent']
    if not s['complete']:
        lines.append('| ' + name + ' | 尚未完成 | | | | | | |')
        continue
    v = s['variants']
    cells = [name] + [f"{v[n]['median']:g}/{v[n]['p95']:g}" for n in 'ABCD']
    cells += [f"{v[n]['percentVs' + ref]:+.2f}%" for n, ref in [('B','A'), ('D','C'), ('D','A')]]
    lines.append('| ' + ' | '.join(cells) + ' |')
lines += ['',
    'seed2 的一次本机 ADB 连接失败发生在安装阶段；重采批次之后另遇 PID17676 的历史 JNI_ERR 崩溃日志误报。旧崩溃时间17:50:49，当前 READY18:56:17，epoch 边界证据确认来源不同。保留52个有效前缀并重测中断位置，最终 ledger 保留1条无效记录。seed3 也遇到本机 ADB 连接失败，复查服务仍为既存PID5972，因此不能断言服务进程重启；保留7个有效前缀并续采。各中断目录保留错误、原因和后续窗口日志。它们不当作应用启动失败，也不计为有效快速启动。', '',
    'seed1独立批D第10轮在电池查询时遇到本机ADB连接超时，此前ThisTime=2331ms且窗口/READY/resumed通过。后续应用仍为PID7678，ADB服务仍为既存PID5972。保留43个有效前缀及1条无效记录，在新目录继续该独立批；中断轮不计入有效时长。', '',
    'seed2独立批A第14轮在Activity查询时也遇到本机5037超时，保留56个有效前缀与无效轮，复查应用PID31311及原服务PID5972仍在。两套ADB版本均37.0.0-14910828，但exe/DLL哈希不同；随后改用与既有服务同目录的SDK客户端续采，后续seed3和最终检查也使用该客户端。记录了环境变更，不宣称已证明超时根因。', '',
    'SDK客户端随后在seed2独立批A第21轮安装阶段再次超时，切换路径未解决问题。保存87个有效样本及两条无效记录，再在非受限工具执行环境中从该轮续采，以检验网络限制假设；原ADB服务及上一有效应用进程仍存活，未重启服务。是否由执行限制导致尚未证明。', '',
    'seed3独立批B第4轮在非受限执行环境中仍遇到相同5037超时；保留17个有效样本及无效轮。由此不能认定受限环境是原因。后续启用任务自有的本地5038服务（PID26740），连接同一设备并续采，保留原5037服务（PID5972）；临时服务在测试结束后单独关闭。所有后续计划记录服务端口变化。', '',
    '## 热点', '',
    '独立 ART harness 使用实际生产生成器转换的8个方法/9个场景。大方法为7896字节，array payload512字节；不是替代业务 APK 的性能结论。每变体正反两批，每批5次预热+10次测量，总计20个有效测量；按最快校准值约1.5秒选共同 iterations。每轮 Java 独立检查结果；正式库没有计数探针。表中门槛按 D/A；不足1秒的测量明确标注，不能用于通过规程。完整 C/A、离散系数、所有变体 P95 与样本数在 JSON 中。', '',
    'largeShort的首次校准为A/B每10000次约4.3/4.4秒，C/D约9毫秒；规则选定1610081次后旧模式每轮预计695–712秒，超出原600秒看门狗，严格三组约35小时。该场景预热已中断，无原严格批次样本被接受；保留前八项和全部校准数据。偏好问题等待期间未获回复，先按各10000次、相同5/20轮数做单独有限时长诊断，不当作正式通过；严格长时间计划仍保留。该决定由执行方明确说明，不声称已获得用户回复或批准。']
for name, seed in report['seeds'].items():
    lines += ['', '### ' + name, '',
        '| 场景 | A 中位/P95 ms | D 中位/P95 ms | B/A | D/C | D/A | 判定 |',
        '| --- | --- | --- | --- | --- | --- | --- |']
    for case, c in seed['hotspots'].get('cases', {}).items():
        v = c['variants']
        cells = [case] + [f"{v[n]['median']/1e6:.2f}/{v[n]['p95']/1e6:.2f}" for n in 'AD']
        cells += [f"{v[n]['percentVs' + ref]:+.2f}%" for n, ref in [('B','A'), ('D','C'), ('D','A')]]
        cells += [('仅诊断；时长规程未满足' if c.get('durationLimitedDiagnostic') else ('≤10%' if c['timingGatePassed'] else '>10%') + ('' if c['durationProtocolMet'] else '；有不足1秒样本'))]
        lines.append('| ' + ' | '.join(cells) + ' |')
    if not seed['hotspots']['complete']:
        lines += ['', '本组热点尚未全部完成。']
lines += ['', '## 分阶段定位和受限优化实验', '',
    '在相同 seed1 输入、API26/O-MVLL 构建上追加 A、纯 S1(18dc5b1)、当前 legacy(L)、C、独立 word 优化(O) 对照。每项均20个有效样本。算术中位数分别1430.52、1732.53、2376.91、3641.78、3285.01ms；短方法1976.57、1989.80、2224.43、1491.61、1464.28ms。S1 算术+21.11%，L +66.16%，C +154.58%；这也说明默认 legacy 的当前路径不能被声称性能与原版一致。', '',
    'O 仅把偶数字节位置的16位读取合并为一次密钥块查询；64字节单窗口、域切换失效、边界检查、缓存擦除都保留。Release/ASan+UBSan 各1312133次差分比较输出、失败状态和完整缓存一致。该隔离构建算术比C降低9.80%，但相对原基线仍+129.64%，未达到门槛。它没有并入生产模板，也没有替换正式三组测试库；O-MVLL构建变化和单seed实验不构成普遍加速保证。', '',
    '第二个独立实验只在解释器入口固定本次调用的reader存在标志与寄存器容量，容量仍条件读取，所有边界/对象/调用检查保留。实际调用方使用局部const vmCode，未发现合法的调用中元数据修改。真实OpenJDK/JNI主机8/8验证通过；两份API26/O-MVLL副本构建前后完整源码清单一致。API27平衡批次20样本：优化legacy算术相对L +0.92%、短方法 -1.47%；优化按需算术相对C -1.86%、短方法 -2.64%。按需算术仍相对A +122.14%，不足以改变超限结论，未并入正式模板。原始日志、host生命周期证据和汇总分别位于diagnosis-invariant、invariant-host与s5-diagnostic-results.json。', '',
    'S1、当前legacy、当前按需和两种隔离优化已有独立增量证据；S2/S3/S4尚未逐个版本独立计时，不把这些合并差值冒充每阶段精确CPU占比。', '',
    '## 分配、读取与内存', '',
    '只在独立 A/C 副本链接包装 malloc/calloc/realloc/free；逐字节 reader 探针不用于正式计时。9个场景各100次驱动：C 这些小寄存器帧场景的 wrapped native allocation 为0（生成 wrapper 与小参数调用使用栈数组）；A 普通场景100次、异常200次、递归900次、四线程400次。不能推导 ART/依赖库/mmap 分配为0，也不是无泄漏证明。', '',
    'largeShort 的 A 每次请求7896字节；C 每次只读取6字节、最大偏移7、一次8字节密钥块 miss。算术/分支 C 每次分别4012/4410字节重复读取和1805/2003次块 miss，支持调查反复域切换的派生成本，不能据此分配CPU耗时占比。PAYLOAD/TRIES探针覆盖数组及异常辅助读取。', '',
    'APK PSS 取启动后的 dumpsys 样本；API27 输出为 TOTAL 而不是 TOTAL PSS，汇总从保留原始日志回读，未修改历史 ledger。热点 VmHWM 是含 ART 的整个进程峰值RSS，VmRSS是退出前RSS，均在计时区间后采集。详情在 s5-summary.json，APK/SO体积和hash在 sizes.json；没有另造内存数值门槛。', '',
    '## 功能及结构证据', '',
    '当前 Java 66项通过；原基线48项通过。S1–S4/P5 的 host ASan/UBSan、ARM64 Debug/Release 语义与并发原始证据保留在各阶段目录。正式三组A/B/C/D的526个方法集合精确一致，C/D及无固定seed候选共7份均完成526方法的真实C++ reader/native回验。', '',
    '实际原签名负向APK分别只改变包名、证书摘要、seed配置，均被初始化/字符串池检查拒绝；错误stage0第三段程序被loader -6拒绝，未READY。负向崩溃不混入正常启动性能统计，之后已恢复正常候选。损坏版本/程序、越界、并发FAILED及重入的host/ART证据见S2–S4/P5。', '',
    '冻结阶段的修复前组合候选（不是最终交付）：`nmm-protect/build/on-demand-20260912/final-acceptance/protected-signed.apk`，SHA256 `d6947ef04d9defa93e1245d13f26fa3bf29b1e5283b8e5e0ee5b900eadae5c2b`。codec3/template4，按需+token+root native VM+private linker+stage0 VM；原签名和全部526方法。outer有stage0 VM，无业务解释器、旧key shares、inner DT_NEEDED、TLS或WX LOAD；inner有token/native VM，无legacy/过渡执行入口或测试探针。', '',
    '该冻结阶段的默认legacy兼容候选也已在API27原签名安装启动：`current-legacy-default/protected-signed.apk`，SHA256 `85c10bacc327da1ff5f10b41f74e687f1e7effedfe690511d4af558037a4d2dd`。该兼容构建使用CLI默认API21目标，不能替代同API26配置的性能对照。', '',
    '## 尚未完成的验收', '',
    '除循环性能超限、大方法诊断时长限制、API26及激活业务缺口外，私有loader仍缺APK直接映射/提取布局完整矩阵、应用内可交互端点、装载临时峰值/缺页与长期native heap证据。阶段耗时包含合并阶段；没有将阶段中位数相加冒充总耗时。匿名模块的自动unwinder发现/完整跨帧展开也不在已验证能力中。', '',
    '## 复算与证据', '',
    '从仓库根执行：', '', '```powershell',
    'python nmm-protect/scripts/summarize-enhancement.py nmm-protect/build/on-demand-20260912 nmm-protect/build/on-demand-20260912/s5-summary.json',
    'python nmm-protect/scripts/measure-startup.py --self-test', '```', '',
    '正式采集命令为 `measure-startup.py <startup-plan.json> <新的输出目录>` 和 `measure-hotspots.py <hotspot-plan.json> <新的输出目录>`；不要覆盖已有证据。原始结果、生成/构建脚本、冻结基线补丁与摘要保存在 [S5证据](runs/20260912-ondemand/S5/)，大体积本地产物留在 build/on-demand-20260912。没有部署覆盖 D:/Android/SDK_WSL/nmmp/vm-protect.jar 或 E:/env-tool/nmmp-wsl.sh。', '']
sizes = json.loads((w / 's5/sizes.json').read_text(encoding='utf-8'))
memory_lines = ['', '### 体积与启动后 PSS', '',
    'SO 为 APK 中保护库文件大小，loader ON 时包含压缩加密内层，不能当作运行时映射大小。PSS 为测量轮中定期采样的中位数，单位 KiB；不是峰值。', '',
    '| seed | A/D APK bytes | A/D SO bytes | 首批 A/D PSS KiB | 独立批 A/D PSS KiB |',
    '| --- | --- | --- | --- | --- |']
for name, seed in report['seeds'].items():
    cells = [name]
    cells += ['/'.join(str(sizes[name][n][key]) for n in 'AD') for key in ['apkBytes', 'soBytes']]
    for batch in ['startup', 'startupIndependent']:
        values = seed[batch].get('variants', {})
        cells.append('/'.join(str(values.get(n, {}).get('pssKb', {}).get('median', '待采集')) for n in 'AD'))
    memory_lines.append('| ' + ' | '.join(cells) + ' |')
lines[lines.index('## 功能及结构证据'):lines.index('## 功能及结构证据')] = memory_lines + ['']
if (w / 'final-stability/summary.json').exists():
    stability = json.loads((w / 'final-stability/summary.json').read_text(encoding='utf-8'))
    if (not stability['samePidAllSamples'] or stability['crashMarkers']
            or not stability['mainWindowVisible'] or not stability['resumed']):
        raise ValueError('Final device observation failed')
    lines[lines.index('## 复算与证据'):lines.index('## 复算与证据')] = [
        '冻结阶段曾恢复上述修复前候选，安装后核对APK哈希、当时PID和loader build ID；这不是当前设备状态。'
        + f"连续{stability['elapsedSeconds']:.1f}秒观察中主PID始终为{stability['pid']}，"
        + '未发现所检查的崩溃标记，结束时MainActivity仍首绘可见且resumed。应用处于未激活状态；这只是空闲驻留验证，不能证明完整业务正确或无泄漏。', '']
(root / 'docs/enhancement/S5_RESULTS.md').write_text('\n'.join(lines), encoding='utf-8')
