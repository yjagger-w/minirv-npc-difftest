`timescale 1ns/1ps
module minirv_board_tb;
  reg clk=0,clk125=0;
  always #10 clk=~clk;
  always #4 clk125=~clk125;
  reg reset=1,btn=1;
  wire done,fault,cv,rdwen;
  wire [31:0] gpio,pc,instr,rddata;
  wire [3:0] rd,led;
  // Different legal capacities also exercise the parameterized address bounds.
  minirv_fpga_system #(.ROM_WORDS(64),.RAM_WORDS(64)) system_dut (
    .clk(clk),.reset(reset),.done(done),.fault(fault),.gpio_output(gpio),
    .commit_valid(cv),.commit_pc(pc),.commit_instr(instr),
    .commit_rd_wen(rdwen),.commit_rd(rd),.commit_rd_data(rddata));
  minirv_pynq_z2 #(.HEARTBEAT_BITS(4)) board_dut (
    .clk125(clk125),.btn_reset(btn),.led(led));
  reg valid=0,wr=0,mem_reset=1;
  reg [31:0] addr=32'h80000000,wdata=0;
  reg [3:0] mask=0;
  wire [31:0] rdata;
  wire mem_fault;
  minirv_fpga_ram #(.WORDS(16)) ram_dut (.clk(clk),.reset(mem_reset),
    .valid(valid),.write(wr),.addr(addr),.wdata(wdata),.wmask(mask),
    .rdata(rdata),.fault(mem_fault));
  reg [31:0] rom_addr=0;
  wire [31:0] rom_data;
  wire rom_fault;
  minirv_fpga_rom #(.WORDS(64)) rom_dut (.addr(rom_addr),.rdata(rom_data),.fault(rom_fault));
  integer commits=0,loads=0,stores=0;
  always @(posedge clk) begin
    #1;
    if (reset) begin commits<=0;loads<=0;stores<=0;end
    else if (cv) begin
      commits<=commits+1;
      if (instr[6:0]==7'h03) loads<=loads+1;
      if (instr[6:0]==7'h23) stores<=stores+1;
      if (instr[6:0]==7'h03 && instr[14:12]==3'b100)
        assert(rddata==32'h11 * ({20'b0,instr[31:20]}+32'd1))
          else $fatal(1,"LBU byte lane data mismatch at pc %h",pc);
    end
  end
  // Explicit assertions below check architectural final values; load data is
  // checked against its effective lane in the self-checking ROM branches.
  task automatic store(input [31:0] a,d,input [3:0] m);
    @(negedge clk);addr=a;wdata=d;mask=m;valid=1;wr=1;
    @(posedge clk);#2;wr=0;
  endtask
  task automatic read_expect(input [31:0] a,d);
    addr=a;valid=1;wr=0;#2;
    assert(!mem_fault && rdata===d) else $fatal(1,"RAM read %h got %h expected %h",a,rdata,d);
  endtask
  initial begin : memory_checks
    #2;assert(rom_data==32'h800000b7 && !rom_fault) else $fatal(1,"ROM initial fetch");
    rom_addr=2;#2;assert(rom_fault) else $fatal(1,"ROM alignment");
    rom_addr=256;#2;assert(rom_fault) else $fatal(1,"ROM range");
    rom_addr=4;#2;assert(!rom_fault && rom_data==32'h12345137) else $fatal(1,"ROM asynchronous fetch");
    mem_reset=0;
    read_expect(32'h80000000,0);
    store(32'h80000000,32'h12345678,15);
    read_expect(32'h80000000,32'h12345678);
    for(integer j=0;j<4;j=j+1) begin
      store(32'h80000000+32'(j),32'h11*(32'(j)+1),1);
    end
    read_expect(32'h80000000,32'h44332211);
    read_expect(32'h80000001,32'h00443322);
    read_expect(32'h80000003,32'h00000044);
    store(32'h80000000,32'hff00ee00,4'b1010);
    read_expect(32'h80000000,32'hff33ee11);
    store(32'h8000003c,32'hdeadbeef,15);
    read_expect(32'h8000003c,32'hdeadbeef);
    store(32'h8000003f,32'h99,1);
    read_expect(32'h8000003c,32'h99adbeef);
    addr=32'h80000040;#2;assert(mem_fault && rdata==0) else $fatal(1,"RAM bound");
    store(32'h80000040,32'h77777777,15);
    read_expect(32'h80000000,32'hff33ee11);
    mem_reset=1;store(32'h80000000,0,15);mem_reset=0;
    read_expect(32'h80000000,32'hff33ee11);
    // A write must not become visible before the active edge.
    @(negedge clk);addr=32'h80000004;wdata=32'hcafebabe;mask=15;wr=1;
    #2;assert(rdata==0) else $fatal(1,"store occurred before edge");
    @(posedge clk);#2;wr=0;assert(rdata==32'hcafebabe) else $fatal(1,"store edge");
    valid=0;#2;assert(rdata==0 && !mem_fault) else $fatal(1,"idle memory");
    $display("[PASS] ROM fetch/bounds; RAM word/byte/masked stores, boundary, reset retention and edge timing");
  end
  initial begin : system_checks
    repeat(4) @(negedge clk);reset=0;btn=0;
    wait(done);#2;
    assert(!fault && gpio==32'hA5 && commits==30 && loads==6 && stores==6)
      else $fatal(1,"system result gpio=%h commits=%0d loads=%0d stores=%0d fault=%b",gpio,commits,loads,stores,fault);
    repeat(5) @(negedge clk);
    assert(commits==30 && done) else $fatal(1,"commit after halt");
    $display("[PASS] CPU ROM program: 30 commits, 6 loads, 6 stores, GPIO A5, EBREAK and no post-halt retirement");
    reset=1;btn=1;
    repeat(4) @(negedge clk);
    assert(!done && !fault && gpio==0) else $fatal(1,"system reset");
    assert(board_dut.reset && led[2:0]==0) else $fatal(1,"board reset/lock");
    reset=0;btn=0;
    wait(done);wait(led[0]);#2;
    assert(!fault && gpio==32'hA5 && led[2:0]==3'b101) else $fatal(1,"reset replay or board status");
    $display("[PASS] reset/replay, lock-gated reset release and board LED status");
    @(negedge clk);reset=1;repeat(3) @(negedge clk);
    system_dut.rom.contents[0]=32'd0;reset=0;
    wait(fault);#2;assert(!done) else $fatal(1,"illegal instruction became success");
    $display("[PASS] illegal instruction reports sticky fault, not successful completion");
    $display("BOARD_SELFTEST_PASS");$finish;
  end
  initial begin : reset_release_check
    wait(board_dut.locked);
    @(posedge board_dut.cpu_clk);#1;assert(board_dut.reset) else $fatal(1,"reset released too early 1");
    @(posedge board_dut.cpu_clk);#1;assert(board_dut.reset) else $fatal(1,"reset released too early 2");
    @(posedge board_dut.cpu_clk);#1;assert(!board_dut.reset) else $fatal(1,"reset release missing");
  end
  initial begin #100000;$fatal(1,"board test timeout");end
endmodule
