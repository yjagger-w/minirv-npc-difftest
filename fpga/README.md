# PYNQ-Z2 FPGA baseline

本目录将现有单周期 `minirv_soc` 接入可综合的片上 ROM/RAM，目标为
TUL PYNQ-Z2 / `xc7z020clg400-1`。原 core、SoC 和 reference 语义不变。
**物理下载、板卡运行和频率测量均未执行。**

## 50MHz 板级结果

Vivado 2025.2，125MHz 参考经 MMCM/BUFG 生成 50MHz CPU 时钟。
2026-11-02 的 v4 实现已完成综合、布局布线和静态时序分析：

| 范围 | 分析阶段/周期 | LUT | FF | WNS |
|---|---|---:|---:|---:|
| core | OOC 综合后估算 /8ns | 830 | 681 | -0.459ns |
| SoC | OOC 综合后估算 /8ns | 963 | 722 | -1.022ns |
| 板级系统 v4 | 布线后 /20ns | 918 | 319 | +2.595ns |

板级 TNS=0，WHS=+0.160ns，THS=0；128 个 LUT 用作 RAM，0 BRAM/DSP，
1 MMCM、2 BUFG。ROM/RAM 默认各 1KiB。ROM 是构建时固定镜像，debug/commit
不引出；常量传播和可观测性不同会改变资源。**以上差异不是 core 优化收益，
OOC 与板级时序不是同一个问题，也不能从 WNS 推导已验证最高频率。**

## 复现与测试

从仓库根目录运行：

```sh
make -f fpga/board/board.mk board-lint board-test
make regression soc-lint soc-test
# 修改 ROM 内容时才重新生成；生成后需重新仿真/综合。
python3 fpga/board/gen_smoke.py
```

既有回归：Emulator 26/26、RTL 27/27、DiffTest 36/36、SoC 11/11；lint 通过。
新增 Verilator 自检覆盖 ROM 取指/边界、RAM word/byte/mask 写及写入时钟沿、
复位保持、30 次提交、GPIO A5、EBREAK、复位重跑及非法指令负例。
Windows XSim 的 UNISIM RTL 和 routed 功能网表测试均通过；后者没有 SDF，
时序结论来自布线后 STA。主 DiffTest 不包含 MMIO/UART reference。

Windows PowerShell，设置实际安装目录和仓库路径，输出必须是新目录：

```powershell
$vivado='D:\Xilinx\2025.2\Vivado\bin\vivado.bat'
$src=(Resolve-Path '.').Path
$out=Join-Path $src 'build\pynq-z2-50mhz'
& $vivado -mode batch -nojournal -nolog -source "$src\fpga\board\implement.tcl" -tclargs $src $out
if($LASTEXITCODE -ne 0){throw 'Implementation failed'}
& $vivado -mode batch -nojournal -nolog -source "$src\fpga\board\postroute_audit.tcl" -tclargs $out
if($LASTEXITCODE -ne 0){throw 'Netlist export failed'}
& "$src\fpga\board\xsim.ps1" -SourceRoot $src -RunRoot "$src\build\xsim-rtl"
& "$src\fpga\board\xsim.ps1" -SourceRoot $src -RunRoot "$src\build\xsim-netlist" -Netlist "$out\board-functional.v"
```

工具基线：Verilator 5.050、GNU Make 4.3、g++11.4.0、Python3.10.12；
Vivado/XSim 2025.2 SW build 6299465。Verilator 的 MMCM 行为模型不用于综合；
XSim 使用实际 UNISIM 原语。详见 [板级契约与约束](board/README.md)。

## Bitstream

同一 v4 routed DCP 已实际执行 write_bitstream，退出码0、DRC预检0 errors。
原始 `.bit` 大小4,045,690 bytes，SHA-256：

```text
900c325e348ec74295ca06c6847710261e1221501fbcadd4474fc7db2ccecec0
```

该值标识已归档文件；重新构建/生成时的时间戳等可能使新文件校验值不同。
生成方法（保留原 DRC，不连接硬件）：

```powershell
$bit=Join-Path $out 'minirv_pynq_z2_50mhz.bit'
& $vivado -mode batch -nojournal -nolog -source "$src\fpga\board\write_bitstream.tcl" -tclargs "$out\routed.dcp" $bit
if($LASTEXITCODE -ne 0){throw 'Bitstream generation failed; inspect original DRC'}
Get-FileHash -LiteralPath $bit -Algorithm SHA256
```

本仓库提交源代码、固定 ROM 初始化材料、脚本/XDC、来源与精简结果；
完整报告/日志和二进制在独立验收归档中，不提交 DCP、bitstream、大波形或缓存。

## 上板前提与限制

ZPS7-1 保留原 Warning：本纯 PL 设计没有 PS7 实例，未提供 PS/DDR/MIO 初始化。
bitgen 成功不等于已证明 PS 默认配置或冷启动可用；不降级、屏蔽 DRC 或执行
来源不明的 ps7_init/寄存器写入来绕过。

厂家手册说明 H16 125MHz 来自 PHY，PHYRSTB(MIO9) 为低会停钟。必须核对实际
板卡版本、供电/启动方式及下载前后的参考。SD启动后JTAG下载与纯冷启动是
不同验收场景；SD成功启动不证明 miniRV 通过。PHY 自身能上电自协商，
不能因此一概声称必须先启动 Linux，也不能保证所有冷启动均有参考。

!LOCKED 保持 CPU 复位，三级 FF 同步释放；MMCM RST 只接 BTN0，参考恢复后
需用 BTN0 复位，不承诺自动失锁重启。本版没有 ILA，无法单凭普通 Hardware
Manager 连接直接观测 LOCKED/reset。

预期正常 LED0亮、LED1灭、LED2亮、LED3约2.98Hz闪烁；BTN0释放后应重跑。
配置 DONE 与 LED0/EBREAK 不同，物理板测仍为**未执行**。排查顺序为下载状态
→参考时钟→MMCM锁定→CPU复位→程序结果。断电后需重新JTAG下载；不提供 BOOT.bin
或 Flash 自动启动方案。来源及校验值见 [pynq-provenance.txt](pynq-provenance.txt)。
