module minirv_bus (
    input         cpu_valid,
    input         cpu_write,
    input  [31:0] cpu_addr,
    input  [31:0] cpu_wdata,
    input  [3:0]  cpu_wmask,
    output [31:0] cpu_rdata,

    output        ram_valid,
    output        ram_write,
    output [31:0] ram_addr,
    output [31:0] ram_wdata,
    output [3:0]  ram_wmask,
    input  [31:0] ram_rdata,

    output        gpio_valid,
    output        gpio_write,
    output [31:0] gpio_addr,
    output [31:0] gpio_wdata,
    output [3:0]  gpio_wmask,
    input  [31:0] gpio_rdata,

    output        bus_error
);

  wire select_ram = (cpu_addr >= 32'h80000000) &&
                    (cpu_addr <= 32'h87ffffff);
  wire select_gpio_input = (cpu_addr >= 32'h10000000) &&
                           (cpu_addr <= 32'h10000003);
  wire select_gpio_output = (cpu_addr >= 32'h10000004) &&
                            (cpu_addr <= 32'h10000007);
  wire select_gpio = select_gpio_input || select_gpio_output;
  wire [1:0] gpio_byte_offset = cpu_addr[1:0];

  assign ram_valid = cpu_valid && select_ram;
  assign ram_write = cpu_write;
  assign ram_addr = cpu_addr;
  assign ram_wdata = cpu_wdata;
  assign ram_wmask = cpu_wmask;

  assign gpio_valid = cpu_valid && select_gpio;
  assign gpio_write = cpu_write;
  assign gpio_addr = {cpu_addr[31:2], 2'b00};
  assign gpio_wdata = cpu_wdata << (gpio_byte_offset * 8);
  assign gpio_wmask = cpu_wmask << gpio_byte_offset;

  assign cpu_rdata = select_ram ? ram_rdata :
                     select_gpio ? (gpio_rdata >> (gpio_byte_offset * 8)) : 32'd0;
  assign bus_error = cpu_valid && !select_ram && !select_gpio;

endmodule
