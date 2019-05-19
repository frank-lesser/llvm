; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -mergeicmps -mtriple=x86_64-unknown-unknown -S | FileCheck %s

%S = type { i32, i32, i32 }

; Last block does not produce the non-constant value into the phi.
; We could handle this case, but an easier way would be to allow other transformations such as
; SimplifyCFG to remove %land.rhs.i.2 and turn the terminator of %land.rhs.i into an unconditional
; branch.

define zeroext i1 @opeq1(
; CHECK-LABEL: @opeq1(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[FIRST_I:%.*]] = getelementptr inbounds [[S:%.*]], %S* [[A:%.*]], i64 0, i32 0
; CHECK-NEXT:    [[TMP0:%.*]] = load i32, i32* [[FIRST_I]], align 4
; CHECK-NEXT:    [[FIRST1_I:%.*]] = getelementptr inbounds [[S]], %S* [[B:%.*]], i64 0, i32 0
; CHECK-NEXT:    [[TMP1:%.*]] = load i32, i32* [[FIRST1_I]], align 4
; CHECK-NEXT:    [[CMP_I:%.*]] = icmp eq i32 [[TMP0]], [[TMP1]]
; CHECK-NEXT:    br i1 [[CMP_I]], label [[LAND_RHS_I:%.*]], label [[OPEQ1_EXIT:%.*]]
; CHECK:       land.rhs.i:
; CHECK-NEXT:    [[SECOND_I:%.*]] = getelementptr inbounds [[S]], %S* [[A]], i64 0, i32 1
; CHECK-NEXT:    [[TMP2:%.*]] = load i32, i32* [[SECOND_I]], align 4
; CHECK-NEXT:    [[SECOND2_I:%.*]] = getelementptr inbounds [[S]], %S* [[B]], i64 0, i32 1
; CHECK-NEXT:    [[TMP3:%.*]] = load i32, i32* [[SECOND2_I]], align 4
; CHECK-NEXT:    [[CMP3_I:%.*]] = icmp eq i32 [[TMP2]], [[TMP3]]
; CHECK-NEXT:    br i1 [[CMP3_I]], label [[LAND_RHS_I_2:%.*]], label [[OPEQ1_EXIT]]
; CHECK:       land.rhs.i.2:
; CHECK-NEXT:    br label [[OPEQ1_EXIT]]
; CHECK:       opeq1.exit:
; CHECK-NEXT:    [[TMP4:%.*]] = phi i1 [ false, [[ENTRY:%.*]] ], [ false, [[LAND_RHS_I]] ], [ [[CMP3_I]], [[LAND_RHS_I_2]] ]
; CHECK-NEXT:    ret i1 [[TMP4]]
;
  %S* nocapture readonly dereferenceable(12) %a,
  %S* nocapture readonly dereferenceable(12) %b) local_unnamed_addr #0 {
entry:
  %first.i = getelementptr inbounds %S, %S* %a, i64 0, i32 0
  %0 = load i32, i32* %first.i, align 4
  %first1.i = getelementptr inbounds %S, %S* %b, i64 0, i32 0
  %1 = load i32, i32* %first1.i, align 4
  %cmp.i = icmp eq i32 %0, %1
  br i1 %cmp.i, label %land.rhs.i, label %opeq1.exit

land.rhs.i:
  %second.i = getelementptr inbounds %S, %S* %a, i64 0, i32 1
  %2 = load i32, i32* %second.i, align 4
  %second2.i = getelementptr inbounds %S, %S* %b, i64 0, i32 1
  %3 = load i32, i32* %second2.i, align 4
  %cmp3.i = icmp eq i32 %2, %3
  br i1 %cmp3.i, label %land.rhs.i.2, label %opeq1.exit

land.rhs.i.2:
  br label %opeq1.exit

opeq1.exit:
  %4 = phi i1 [ false, %entry ], [ false,  %land.rhs.i], [ %cmp3.i, %land.rhs.i.2 ]
  ret i1 %4
}
