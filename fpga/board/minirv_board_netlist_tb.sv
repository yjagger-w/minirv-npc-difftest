`timescale 1ns/1ps
module minirv_board_netlist_tb;
  reg clk125=0,btn_reset=1;
  always #4 clk125=~clk125;
  wire [3:0] led;
  minirv_pynq_z2 dut(.clk125(clk125),.btn_reset(btn_reset),.led(led));
  initial begin
    #128;btn_reset=0;
    wait(led[0]===1'b1);#1;
    assert(led[2:0]===3'b101) else $fatal(1,"netlist boot failed: %b",led);
    #37;btn_reset=1;#200;btn_reset=0;
    wait(led[0]===1'b0);
    wait(led[0]===1'b1);#1;
    assert(led[2:0]===3'b101) else $fatal(1,"netlist reset replay failed: %b",led);
    $display("POST_ROUTE_FUNCTIONAL_PASS: initialized ROM/LUTRAM, MMCM, boot and reset replay");
    $finish;
  end
  initial begin #100000;$fatal(1,"netlist timeout");end
endmodule
