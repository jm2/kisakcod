#include "scr_parsetree.h"
#include <universal/assertive.h>
#include <universal/com_memory.h>
#include "scr_evaluate.h"
#include "scr_vm.h"

//struct debugger_sval_s *g_debugExprHead 83123658     scr_parsetree.obj

HunkUser *g_allocNodeUser;

void __cdecl Scr_InitAllocNode()
{
    if (g_allocNodeUser)
        MyAssertHandler(".\\script\\scr_parsetree.cpp", 66, 0, "%s", "!g_allocNodeUser");
    g_allocNodeUser = Hunk_UserCreate(0x10000, "Scr_InitAllocNode", 0, 1, 7);
}

void __cdecl Scr_ShutdownAllocNode()
{
    if (g_allocNodeUser)
    {
        Hunk_UserDestroy(g_allocNodeUser);
        g_allocNodeUser = 0;
    }
}

//SCRIPT_DEBUGGER_ORDINARY_NODES_BEGIN
sval_u *__cdecl Scr_AllocNode(int size)
{
    if (!g_allocNodeUser)
        MyAssertHandler(".\\script\\scr_parsetree.cpp", 82, 0, "%s", "g_allocNodeUser");
    return static_cast<sval_u *>(Hunk_UserAlloc(g_allocNodeUser, sizeof(sval_u) * size, alignof(sval_u)));
}

sval_u __cdecl node0(Enum_t type)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(1);
    result.node[0].type = type;
    return result;
}

sval_u __cdecl node1(Enum_t type, sval_u val1)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(2);
    result.node[0].type = type;
    result.node[1] = val1;
    return result;
}

sval_u __cdecl node2(Enum_t type, sval_u val1, sval_u val2)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(3);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    return result;
}

sval_u __cdecl node3(Enum_t type, sval_u val1, sval_u val2, sval_u val3)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(4);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    return result;
}

sval_u __cdecl node4(Enum_t type, sval_u val1, sval_u val2, sval_u val3, sval_u val4)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(5);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    return result;
}

sval_u __cdecl node5(Enum_t type, sval_u val1, sval_u val2, sval_u val3, sval_u val4, sval_u val5)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(6);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    result.node[5] = val5;
    return result;
}

sval_u __cdecl node6(Enum_t type, sval_u val1, sval_u val2, sval_u val3, sval_u val4, sval_u val5, sval_u val6)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(7);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    result.node[5] = val5;
    result.node[6] = val6;
    return result;
}

sval_u __cdecl node7(
    Enum_t type,
    sval_u val1,
    sval_u val2,
    sval_u val3,
    sval_u val4,
    sval_u val5,
    sval_u val6,
    sval_u val7)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(8);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    result.node[5] = val5;
    result.node[6] = val6;
    result.node[7] = val7;
    return result;
}

sval_u __cdecl node8(
    Enum_t type,
    sval_u val1,
    sval_u val2,
    sval_u val3,
    sval_u val4,
    sval_u val5,
    sval_u val6,
    sval_u val7,
    sval_u val8)
{
    sval_u result; // eax

    result.node = Scr_AllocNode(9);
    result.node[0].type = type;
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    result.node[5] = val5;
    result.node[6] = val6;
    result.node[7] = val7;
    result.node[8] = val8;
    return result;
}

//SCRIPT_DEBUGGER_ORDINARY_NODES_END

// Decomp Status: Tested, Completed
//SCRIPT_DEBUGGER_LINKED_LIST_END_BEGIN
sval_u linked_list_end(sval_u val1)
{
    sval_u *node;
    sval_u result;

    node = Scr_AllocNode(2);
    node[0] = val1;
    node[1].node = nullptr;
    result.node = Scr_AllocNode(2);
    result.node[0].node = node;
    result.node[1].node = node;
    return result;
}
//SCRIPT_DEBUGGER_LINKED_LIST_END_END

// Decomp Status: Tested, Completed
//SCRIPT_DEBUGGER_PREPEND_NODE_BEGIN
sval_u prepend_node(sval_u val1, sval_u val2)
{
    sval_u *node;

    node = Scr_AllocNode(2);
    node[0] = val1;
    node[1] = *val2.node;
    val2.node->node = node;
    return val2;
}
//SCRIPT_DEBUGGER_PREPEND_NODE_END

// Decomp Status: Tested, Completed
//SCRIPT_DEBUGGER_APPEND_NODE_BEGIN
sval_u append_node(sval_u val1, sval_u val2)
{
    sval_u *node;

    node = Scr_AllocNode(2);
    node[0] = val2;
    node[1].node = nullptr;
    val1.node[1].node[1].node = node;
    val1.node[1].node = node;
    return val1;
}

//SCRIPT_DEBUGGER_APPEND_NODE_END

void __cdecl Scr_ClearDebugExpr(debugger_sval_s *debugExprHead)
{
    while (debugExprHead)
    {
        //Scr_ClearDebugExprValue((sval_u)(uintptr_t)&debugExprHead[1]);

        // See Prefixed data in Scr_AllocDebugExpr()
        sval_u *pval = (sval_u *)((char *)debugExprHead + sizeof(debugger_sval_s));
        Scr_ClearDebugExprValue(*(sval_u *)&pval);

        debugExprHead = debugExprHead->next;
    }
}

//SCRIPT_DEBUGGER_SCR_ALLOCDEBUGEXPR_BEGIN
sval_u *__cdecl Scr_AllocDebugExpr(Enum_t type, int size, const char *name)
{
    sval_u *val; // eax
    debugger_sval_s *debugval;

    // prefix the malloc with a `debugger_sval_s`
    unsigned char *data = (unsigned char*)Z_Malloc(sizeof(debugger_sval_s) + size, name, 0);

    debugval = (debugger_sval_s *)data;
    val = (sval_u *)(data + sizeof(debugger_sval_s));

    // prepend the global list
    debugval->next = g_debugExprHead;
    g_debugExprHead = debugval;

    // set val type (convenience vs. the non-debug way) and return it
    val->node = nullptr; // ENUM_NOP doubles as an initially empty list head.
    val->type = type;
    return val;
}
//SCRIPT_DEBUGGER_SCR_ALLOCDEBUGEXPR_END

void __cdecl Scr_FreeDebugExpr(ScriptExpression_t *expr)
{
    debugger_sval_s *debugExprHead; // [esp+0h] [ebp-Ch]
    debugger_sval_s *nextDebugExprHead; // [esp+8h] [ebp-4h]

    if (expr->breakonExpr)
        --scrVmDebugPub.checkBreakon;

    debugExprHead = expr->exprHead;

    iassert(debugExprHead);

    while (debugExprHead)
    {
        // See Prefixed data in Scr_AllocDebugExpr()
        sval_u *pval = (sval_u *)((char *)debugExprHead + sizeof(debugger_sval_s));
        Scr_FreeDebugExprValue(*(sval_u*)&pval);

        nextDebugExprHead = debugExprHead->next;
        Z_Free(debugExprHead, 0);
        debugExprHead = nextDebugExprHead;
    }
}

// M4 (ki-n1et): the debugger parse-node builders size their allocations in
// 4-byte sval_u cells. The cell widened to sizeof(sval_u) (8 on native64),
// so every node's trailing cell count is expressed in full cells -- the
// node[N] writes below index widened cells.

//SCRIPT_DEBUGGER_DEBUGGER_NODE0_BEGIN
sval_u __cdecl debugger_node0(Enum_t type)
{
    sval_u result;
    result.node = Scr_AllocDebugExpr(type, sizeof(sval_u), "debugger_node0");
    return result;
}
//SCRIPT_DEBUGGER_DEBUGGER_NODE0_END

//SCRIPT_DEBUGGER_DEBUGGER_NODE1_BEGIN
sval_u __cdecl debugger_node1(Enum_t type, sval_u val1)
{
    sval_u result; // eax

    result.node = Scr_AllocDebugExpr(type, 2 * sizeof(sval_u), "debugger_node1");
    result.node[1] = val1;

    return result;
}
//SCRIPT_DEBUGGER_DEBUGGER_NODE1_END

//SCRIPT_DEBUGGER_DEBUGGER_NODE2_BEGIN
sval_u __cdecl debugger_node2(Enum_t type, sval_u val1, sval_u val2)
{
    sval_u result; // eax

    result.node = Scr_AllocDebugExpr(type, 3 * sizeof(sval_u), "debugger_node2");
    result.node[1] = val1;
    result.node[2] = val2;
    return result;
}
//SCRIPT_DEBUGGER_DEBUGGER_NODE2_END

//SCRIPT_DEBUGGER_DEBUGGER_NODE3_BEGIN
sval_u __cdecl debugger_node3(Enum_t type, sval_u val1, sval_u val2, sval_u val3)
{
    sval_u result; // eax

    result.node = Scr_AllocDebugExpr(type, 4 * sizeof(sval_u), "debugger_node3");
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    return result;
}
//SCRIPT_DEBUGGER_DEBUGGER_NODE3_END

//SCRIPT_DEBUGGER_DEBUGGER_NODE4_BEGIN
sval_u __cdecl debugger_node4(Enum_t type, sval_u val1, sval_u val2, sval_u val3, sval_u val4)
{
    sval_u result; // eax

    result.node = Scr_AllocDebugExpr(type, 5 * sizeof(sval_u), "debugger_node4");
    result.node[1] = val1;
    result.node[2] = val2;
    result.node[3] = val3;
    result.node[4] = val4;
    return result;
}
//SCRIPT_DEBUGGER_DEBUGGER_NODE4_END

//SCRIPT_DEBUGGER_DEBUGGER_PREPEND_NODE_BEGIN
sval_u __cdecl debugger_prepend_node(sval_u val1, sval_u val2)
{
    sval_u head = debugger_node2(ENUM_NOP, val1, *val2.node);
    val2.node->node = &head.node[1];
    return val2;
}
//SCRIPT_DEBUGGER_DEBUGGER_PREPEND_NODE_END

//SCRIPT_DEBUGGER_DEBUGGER_BUFFER_BEGIN
sval_u __cdecl debugger_buffer(Enum_t type, char *buf, uint32_t size, int alignment)
{
    sval_u *result; // [esp+4h] [ebp-8h]
    uint8_t *bufCopy; // [esp+8h] [ebp-4h]
    int alignmenta; // [esp+20h] [ebp+14h]

    if ((alignment & (alignment - 1)) != 0)
        MyAssertHandler((char *)".\\script\\scr_parsetree.cpp", 594, 0, "%s", "IsPowerOf2( alignment )");
    alignmenta = alignment - 1;
    // M4 (ki-n1et): the trailing bytes cover result[0..1] before the
    // aligned copy area starts at &result[2]; size those cells with the
    // widened cell instead of the literal 8.
    result = Scr_AllocDebugExpr(type, size + alignmenta + 2 * sizeof(sval_u), "debugger_buffer");
    bufCopy = reinterpret_cast<uint8_t *>(
        ~static_cast<uintptr_t>(alignmenta) &
        (reinterpret_cast<uintptr_t>(&result[2]) + alignmenta));
    memcpy(bufCopy, (uint8_t *)buf, size);
    // M4 (ki-n1et): store the buffer pointer through the pointer member.
    // The retail `(int)bufCopy` store truncated every pointer above 4 GiB
    // on native64 while the consumers (Scr_EvalPrimitiveExpression /
    // Scr_CompilePrimitiveExpression for ENUM_string/ENUM_istring) read
    // the slot back through `.debugString`.
    result[1].debugString = reinterpret_cast<const char *>(bufCopy);
    sval_u value;
    value.node = result;
    return value;
}
//SCRIPT_DEBUGGER_DEBUGGER_BUFFER_END

//SCRIPT_DEBUGGER_DEBUGGER_STRING_BEGIN
sval_u __cdecl debugger_string(Enum_t type, char *s)
{
    return debugger_buffer(type, s, strlen(s) + 1, 1);
}
//SCRIPT_DEBUGGER_DEBUGGER_STRING_END
