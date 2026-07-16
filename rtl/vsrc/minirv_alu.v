module minirv_alu (
    input  [31:0] lhs,
    input  [31:0] rhs,
    input         subtract,
    output [31:0] sum
);

  assign sum = subtract ? lhs - rhs : lhs + rhs;

endmodule
