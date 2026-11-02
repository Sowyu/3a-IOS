; The compute self-test's kernel, as AIR: Metal source equivalent
;   struct U { uint n, k; };
;   kernel void chl_selftest(constant U &u [[buffer(0)]], device const uint *in [[buffer(1)]], device uint *out [[buffer(2)]],
;                            device half *outh [[buffer(3)]], uint gid [[thread_position_in_grid]])
;   { if (gid < u.n) { out[gid] = in[gid] * u.k + gid; outh[gid] = half(gid); } }
; Written by hand on PencilKit's sdfPen_kernel (same AIR version, address spaces and metadata shape) because this Mac has no
; Metal toolchain. The half store is PencilKit's case: its kernels write half fields of vertex structs, two to a 32-bit word.
; Rebuild the embedded copy with: llvm-as chl_selftest.ll -o chl_selftest.bc && xxd -i chl_selftest.bc > chl_selftest_air.h
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-ios26.6.0"

%struct.U = type { i32, i32 }

define void @chl_selftest(ptr addrspace(2) noundef readonly align 4 captures(none) dereferenceable(8) "air-buffer-no-alias" %0, ptr addrspace(1) noundef readonly captures(none) "air-buffer-no-alias" %1, ptr addrspace(1) noundef writeonly captures(none) "air-buffer-no-alias" %2, ptr addrspace(1) noundef writeonly captures(none) "air-buffer-no-alias" %3, i32 noundef %4) local_unnamed_addr #0 {
  %6 = getelementptr inbounds %struct.U, ptr addrspace(2) %0, i64 0, i32 0
  %7 = load i32, ptr addrspace(2) %6, align 4
  %8 = icmp ult i32 %4, %7
  br i1 %8, label %9, label %20

9:
  %10 = getelementptr inbounds %struct.U, ptr addrspace(2) %0, i64 0, i32 1
  %11 = load i32, ptr addrspace(2) %10, align 4
  %12 = zext i32 %4 to i64
  %13 = getelementptr inbounds i32, ptr addrspace(1) %1, i64 %12
  %14 = load i32, ptr addrspace(1) %13, align 4
  %15 = mul i32 %14, %11
  %16 = add i32 %15, %4
  %17 = getelementptr inbounds i32, ptr addrspace(1) %2, i64 %12
  store i32 %16, ptr addrspace(1) %17, align 4
  %18 = tail call half @air.convert.f.f16.u.i32(i32 %4)
  %19 = getelementptr inbounds half, ptr addrspace(1) %3, i64 %12
  store half %18, ptr addrspace(1) %19, align 2
  br label %20

20:
  ret void
}

declare half @air.convert.f.f16.u.i32(i32) local_unnamed_addr #1

attributes #0 = { convergent mustprogress nofree nosync nounwind "frame-pointer"="all" }
attributes #1 = { mustprogress nofree nosync nounwind willreturn memory(none) }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}
!air.version = !{!10}
!air.language_version = !{!11}
!air.compile_options = !{!12, !13}
!air.kernel = !{!15}

!0 = !{i32 2, !"SDK Version", [3 x i32] [i32 26, i32 6, i32 1]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{!"chl-selftest, hand-written"}
!10 = !{i32 2, i32 8, i32 0}
!11 = !{!"Metal", i32 4, i32 0, i32 0}
!12 = !{!"air.compile.denorms_disable"}
!13 = !{!"air.compile.fast_math_enable"}
!15 = !{ptr @chl_selftest, !16, !17}
!16 = !{}
!17 = !{!18, !20, !21, !22, !23}
!18 = !{i32 0, !"air.buffer", !"air.buffer_size", i32 8, !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !19, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"U", !"air.arg_name", !"u"}
!19 = !{i32 0, i32 4, i32 0, !"uint", !"n", i32 4, i32 4, i32 0, !"uint", !"k"}
!20 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"in"}
!21 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!22 = !{i32 3, !"air.buffer", !"air.location_index", i32 3, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 2, !"air.arg_type_align_size", i32 2, !"air.arg_type_name", !"half", !"air.arg_name", !"outh"}
!23 = !{i32 4, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"gid"}
