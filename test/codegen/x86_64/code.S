# RUN: %opt - -triple x86_64
  .section .text
ref_code:
  mov.i64  $0, caml_code_begin
  ret.i64  $0
