#include "cg_local.h"
#include "cg_public.h"

#include <bgame/bg_local.h>
#include <cgame/cg_pose_atomic.h>

void __cdecl CG_UsedDObjCalcPose(cpose_t *pose)
{
    iassert(pose);
    cg::pose_atomic::MarkUsed(&pose->cullIn);
}

void __cdecl CG_CullIn(cpose_t *pose)
{
    iassert(pose);
    cg::pose_atomic::MarkCulled(&pose->cullIn);
}
