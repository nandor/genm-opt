# RUN: %opt - -triple x86_64

  .section .text
f:
  .call caml
  .args v64

  arg.v64          $2, 0
  mov.i64          $0, g
  call.i64.caml    $1, $0 @caml_frame()
  ret              $2
