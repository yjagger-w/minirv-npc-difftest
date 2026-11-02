param(
 [Parameter(Mandatory=$true)][string]$SourceRoot,
 [Parameter(Mandatory=$true)][string]$RunRoot,
 [string]$Netlist='',
 [string]$VivadoBin='D:\Xilinx\2025.2\Vivado\bin'
)
$ErrorActionPreference='Stop'
$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).ProviderPath
if(Test-Path -LiteralPath $RunRoot){throw 'Use a new simulation output directory'}
New-Item -ItemType Directory -Path (Join-Path $RunRoot 'fpga\board') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $SourceRoot 'fpga\board\smoke.hex') -Destination (Join-Path $RunRoot 'fpga\board\smoke.hex')
if($Netlist){
 $sources=@((Resolve-Path -LiteralPath $Netlist).ProviderPath,(Join-Path $SourceRoot 'fpga\board\minirv_board_netlist_tb.sv'))
 $top='minirv_board_netlist_tb';$pass='POST_ROUTE_FUNCTIONAL_PASS'
}else{
 $sources=@('rtl/vsrc/minirv_regfile.v','rtl/vsrc/minirv_decoder.v','rtl/vsrc/minirv_immgen.v','rtl/vsrc/minirv_alu.v','rtl/vsrc/minirv_core.v','rtl/soc/minirv_bus.v','rtl/peripheral/minirv_gpio.v','rtl/peripheral/minirv_uart_tx.v','rtl/soc/minirv_soc.v','fpga/board/minirv_fpga_memory.sv','fpga/board/minirv_fpga_system.sv','fpga/board/minirv_pynq_z2.sv','fpga/board/minirv_board_tb.sv') | ForEach-Object {Join-Path $SourceRoot $_}
 $top='minirv_board_tb';$pass='BOARD_SELFTEST_PASS'
}
Push-Location -LiteralPath $RunRoot
try{
 & (Join-Path $VivadoBin 'xvlog.bat') --sv @sources (Join-Path $VivadoBin '..\data\verilog\src\glbl.v') 2>&1 | Tee-Object -FilePath 'compile-console.log'
 if($LASTEXITCODE -ne 0){throw 'xvlog failed'}
 & (Join-Path $VivadoBin 'xelab.bat') --debug typical --timescale 1ns/1ps -L unisims_ver -L secureip $top glbl -s board_sim 2>&1 | Tee-Object -FilePath 'elaboration-console.log'
 if($LASTEXITCODE -ne 0){throw 'xelab failed'}
 & (Join-Path $VivadoBin 'xsim.bat') board_sim --runall 2>&1 | Tee-Object -FilePath 'simulation-console.log'
 if($LASTEXITCODE -ne 0 -or -not(Select-String -LiteralPath 'simulation-console.log' -SimpleMatch $pass -Quiet)){throw 'Simulation failed or PASS missing'}
}finally{Pop-Location}
