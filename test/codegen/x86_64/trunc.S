# RUN: %opt - -triple x86_64
  .section .text
_trunc_f32_u32:
  .args        f32
  .call        c
  arg.f32      $0, 0
  zext.i32     $1, $0
  ret          $1
  .end

_trunc_f32_i32:
  .args        f32
  .call        c
  arg.f32      $0, 0
  sext.i32     $1, $0
  ret          $1
  .end

_trunc_f32_i64:
  .args        f32
  .call        c
  arg.f32      $0, 0
  sext.i64    $1, $0
  ret          $1
  .end

_trunc_f64_u32:
  .args        f64
  .call        c
  arg.f64      $0, 0
  zext.i32     $1, $0
  ret          $1
  .end

_trunc_f64_i32:
  .args        f64
  .call        c
  arg.f64      $0, 0
  sext.i32     $1, $0
  ret          $1
  .end

_trunc_f64_i64:
  .args        f64
  .call        c
  arg.f64      $0, 0
  sext.i64    $1, $0
  ret          $1
  .end

_trunc_i64_i32:
  .args        i64
  .call        c
  arg.i64      $0, 0
  trunc.i32    $1, $0
  ret          $1
  .end
