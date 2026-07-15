module minirv_alu (
    input  [31:0] lhs,
    input  [31:0] rhs,
    output [31:0] sum
);

  assign sum = lhs + rhs;

endmodule
