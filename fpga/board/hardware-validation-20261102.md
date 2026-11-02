# PYNQ-Z2 v4：JTAG、断电重下载及视频验收

日期：2026-11-02，Asia/Hong_Kong。使用 Windows Vivado 2025.2
SW Build 6299465 下载已归档的 50MHz v4 bitstream，未重新构建或改 RTL/ROM/XDC。
本记录更新先前“物理板测未执行”的状态；原 OOC、仿真与布线后 STA 基线保持不变。

## 镜像与目标

- 基线源码提交：`7a770112317e31b766313350759332d98dc81473`。
- 文件：`minirv_pynq_z2_50mhz_v4.bit`，4,045,690 bytes。
- SHA-256：`900c325e348ec74295ca06c6847710261e1221501fbcadd4474fc7db2ccecec0`。
- 每次编程入口均重新核验上述 SHA-256。
- 本地 `localhost:3121` 实际只发现一个 FT2232H 下载器目标；
  JTAG 链为 `arm_dap_0`、`xc7z020_1`，FPGA PART=`xc7z020`，
  IDCODE=`23727093`。两次均为同一精确目标，未按列表第一项盲选。
- 构建器件为 `xc7z020clg400-1`。JTAG ID 本身不能证明封装、速度等级或板卡修订版。

## 实际结果及证据来源

| 项目 | 结果 | 依据 |
|---|---|---|
| 首次 JTAG 下载 | 配置通过 | Vivado 退出码 0；DONE internal/pin=1，EOS=1，CRC/IDCODE/security error=0 |
| 首次运行 LED、BTN0 松开后重跑 | 用户观察通过 | 各观察至少2秒的提问后，用户回复“达到预期” |
| 完全断电、重新上电 | 用户确认已执行 | 用户确认断电时电源灯“完全熄灭” |
| 重新下载前 | 已记录初态 | 用户称 LED0–3 全灭；重新枚举读出 DONE internal/pin=0、EOS=0 |
| 断电后 JTAG 重新下载 | 配置通过 | 同一目标、同一 bitstream；退出码 0；DONE internal/pin=1、EOS=1，错误位均0 |
| 重下载后 LED、BTN0 松开后重跑 | 用户观察通过 | 用户回复“1，2均达到预期” |
| 视频正常灯态、心跳 | 视频支持 | LED0/2亮、LED1灭，LED3约3Hz |
| 视频独立确认完整 BTN0 重跑 | 证据不足 | 手指遮挡开关行程和准确按下/松开时刻 |
| 125MHz、PHY reset、用户 MMCM LOCKED | 未直接测量 | 无示波器或对应测量记录；本版无 ILA/VIO |

两次编程 Tcl 的 PROGRAM_START/END 时间（+08:00）分别为
18:37:42–18:38:15、18:45:26–18:45:55。
这些标记包含连接、编程、状态读取及关闭操作；不是纯传输耗时。
退出码和原始日志另行保留。配置寄存器中的 PLL_LOCK_STATUS 不等于
本设计 MMCM 的 LOCKED，不用它证明 CPU 时钟状态。

“达到预期”对应：LED0亮、LED1灭、LED2亮、LED3约2.98Hz闪烁；
按住 BTN0 约0.1秒再松开后恢复相同状态。配置 DONE 与用户 LED0/EBREAK
分别记录，工具配置成功不能代替程序运行结果。结论仅覆盖固定 smoke ROM
及本次观测范围，不是完整 CPU 验证或最大频率测量。

## 启动状态及剩余限制

首次测试由用户报告 JP1=JTAG、SD插入且 PYNQ 已运行；未独立确认精确启动顺序、
SD镜像来源/版本/校验值、自动 overlay 状态或板卡修订版。
断电测试指导保持 JP1/SD 设置不变；用户重新上电后对 PYNQ 是否运行表示不确定。
因此准确记录为当前设置下“完全断电→重新上电→JTAG重下载→用户观察通过”，
不能将其写成已证明的特定 SD 启动流程、所有纯 JTAG 冷启动组合或长期可靠性结论。

H16 的125MHz来自PHY，PHYRSTB(MIO9)为低会停钟；本轮没有直接测量它们。
LED心跳间接支持运行时存在时钟并已释放复位，不替代125MHz频率/PHY电平测量。
ZPS7-1仍为原Warning、无PS7实例，不因本次通过消失或被屏蔽。
没有执行PS初始化脚本或PS/PHY寄存器写入；未写Flash、修改SD/启动镜像。
JTAG配置仍是易失的，不提供无需JTAG重下载的掉电自启动。

参考中断后应先恢复稳定参考，再按BTN0复位MMCM；不承诺自动失锁恢复。
故障排查仍按：下载状态→参考时钟→MMCM锁定→CPU复位→程序结果。
没有ILA/VIO时不能通过普通Hardware Manager直接读取用户LOCKED/reset。

## 视频补充证据

原文件：`a9f6ac990c36591af02dda791c662833.mp4`，2,392,874 bytes。
SHA-256：`776006d3cb91ab9c30dbb5214331175ef18bb73206a1332a5a8db5e23e062e8e`。

视频约21.11秒，解码633帧，原始PTS为0–21.078333秒，约29.98fps。
原竖屏画面从上到下为LED3、LED2、LED1、LED0。
在1.2–3.2秒清楚窗口内，LED3亮起边沿约为：

```text
1.233333, 1.566667, 1.901667, 2.235000, 2.568333, 2.901667 seconds
```

5个完整间隔合计1.668334秒，平均周期约0.334秒，视频估算约3.0Hz，
与预期约2.98Hz一致。约30fps的帧间隔约33ms，不据小数位声称仪器精度。
分析使用原始PTS、RGB绿色光斑分组和逐帧亮灭核对；后段曝光/手部遮挡导致
自动阈值不稳定的片段未纳入频率结论。使用imageio-ffmpeg 0.8.0 /
FFmpeg 9.0.2、Pillow、NumPy/SciPy；只旋转/缩放查看，没有重建细节。

约9–19.5秒可见手指操作/遮挡BTN0区域；准确按下/松开不可可靠辨认，
最终手指离开后的无遮挡尾段不足2秒。完整BTN0重跑通过仍依据用户反馈，
不提升为视频独立证实。视频未清楚覆盖断电→上电→JTAG全过程，
亦不能凭画面独立关联某次下载或bitstream身份。

## 操作复现与归档

先按[上板前提](../README.md#上板前提与限制)核对实物和启动状态，
核验bitstream SHA-256；枚举实际目标，出现多个候选先确认。
实际使用的关键硬件Tcl操作为：

```tcl
open_hw_manager
connect_hw_server -url localhost:3121
get_hw_targets
# 打开已核对的精确目标后，核对器件集合、PART及IDCODE：
get_hw_devices
# 本次已核对器件为 xc7z020_1；只给该器件设置已校验的 PROGRAM.FILE。
program_hw_devices xc7z020_1
refresh_hw_device -update_hw_probes false [get_hw_devices xc7z020_1]
# 检查 DONE internal/pin、EOS 为1，以及CRC/IDCODE/security error为0。
```

以上是实际步骤摘要，不是忽略目标选择/文件校验的可盲跑脚本。
重新下载后观察至少2秒，再按BTN0约0.1秒并松开、观察至少2秒；
断电测试先记录电源灯熄灭和重下载前灯态，再重复核验、枚举和JTAG下载。

原始硬件日志、完整命令、JSON结果和视频复核脚本/帧在操作机器的独立验收目录保留：

- `hardware-v4-20261102-183513/`：首次配置、用户反馈；
- `hardware-v4-powercycle-20261102-184350/`：断电重下载、用户反馈；
- `video-review-v4-20261102-185318/`：原视频SHA、PTS、抽样帧、频率分析和限制。

这些归档不随Git提交。仓库只保存本精简报告；不提交视频、硬件序列号、
完整日志、DCP/bitstream、波形或缓存。源码和构建结果在本轮没有重跑；
资源差异仍不作为core优化收益，50MHz以既有布线后STA为基线。
