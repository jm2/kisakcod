// Production debugger compiler/evaluator and watch bodies with native records.
// Renderer/UI globals and external engine services are fixture doubles.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <script/scr_main.h>
#include <script/scr_compiler.h>
#include <script/scr_evaluate.h>
#include <script/scr_stringlist.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

scrVarPub_t scrVarPub{};
scrVarGlob_t scrVarGlob{};
scrCompilePub_t scrCompilePub{};
scrCompileGlob_t scrCompileGlob{};
scr_classStruct_t g_classMap[CLASS_NUM_COUNT] = {{0, 0, 0, ""}, {0, 0, 0, ""}, {0, 0, 0, ""}, {0, 0, 0, ""}};
debugger_sval_s *g_debugExprHead = nullptr;
int g_breakonExpr = 0;
int g_script_error_level = -1;
jmp_buf g_script_error[33];
struct { int checkBreakon = 0; } scrVmDebugPub;
struct {
    VariableValue stack[32]{};
    VariableValue *top = stack;
    VariableValue *maxstack = stack + 31;
    uint32_t outparamcount = 0;
    uint32_t inparamcount = 0;
    uint32_t breakpointOutparamcount = 0;
    bool debugCode = false;
} scrVmPub;
struct Scr_ScriptWatch {
    uint32_t localId = 0;
    bool PostEvaluateWatchElement(Scr_WatchElement_s *, VariableValue *);
};
namespace {
std::vector<void *> allocations;
float *payload = nullptr;
int checks = 0;
int callbacks = 0;
int methodClass = 0;
int methodLookups = 0;
bool hasCachedMethod = false;
VariableValue cachedMethod{};
char diagnostic[] = "debugger fixture";
void CheckAt(bool okay, const char *expression, int line) {
    ++checks;
    if (!okay) { std::fprintf(stderr, "script debugger:%d: %s failed\n", line, expression); std::abort(); }
}
#define Check(condition) CheckAt((condition), #condition, __LINE__)
void HighAddress(const void *p) {
#if defined(KISAK_REQUIRE_HIGH_POINTERS) && UINTPTR_MAX > 0xffffffffu
    Check(reinterpret_cast<uintptr_t>(p) > UINT32_MAX);
#else
    Check(p != nullptr);
#endif
}
void *Allocate(size_t bytes) { void *p = std::malloc(bytes); if (!p) std::abort(); std::memset(p, 0xA5, bytes); allocations.push_back(p); HighAddress(p); return p; }
}
void MyAssertHandler(const char *, int, int, const char *, ...) { std::abort(); }
void *Z_Malloc(int bytes, const char *, int) { return Allocate(bytes); }
sval_u *Scr_AllocNode(int count) { return static_cast<sval_u *>(Allocate(count * sizeof(sval_u))); }
const char *SL_ConvertToString(uint32_t) { return "fixture"; }
uint16_t Scr_CompileCanonicalString(uint32_t id) { return static_cast<uint16_t>(id + 10); }
uint32_t AllocValue() { return 7; }
uint32_t FindVariable(uint32_t parent, uint32_t) { return parent == 900 && hasCachedMethod ? 7 : 0; }
VariableValue Scr_EvalVariable(uint32_t) { return cachedMethod; }
void SetVariableValue(uint32_t, VariableValue *value) { cachedMethod = *value; hasCachedMethod = true; }
bool IsObjectFree(uint32_t) { return false; }
uint32_t GetObjectType(uint32_t) { return VAR_ENTITY; }
int Scr_GetClassnumForCharId(char) { return 0; }
void AddRefToObject(uint32_t) {}
void RemoveRefToObject(uint32_t) {}
void SL_RemoveRefToString(uint32_t) {}
void AddRefToValue(int type, VariableUnion value) { if (type == VAR_VECTOR) { Check(value.vectorValue == payload); Check(value.vectorValue[2] == 3); } }
void RemoveRefToValue(int type, VariableUnion value) { AddRefToValue(type, value); }
void RemoveRefToVector(const float *p) { Check(p == payload && p[2] == 3); }
void Scr_CastWeakerPair(VariableValue *a, VariableValue *b) { Check(a->type == b->type); }
void Scr_UnmatchingTypesError(VariableValue *, VariableValue *) { std::abort(); }
float I_fabs(float value) { return std::fabs(value); }
void Scr_Error(const char *) { std::abort(); }
char *va(const char *, ...) { return diagnostic; }
void Scr_ClearErrorMessage() { scrVarPub.error_message = nullptr; }
void Scr_ClearOutParams() { scrVmPub.top -= scrVmPub.outparamcount; scrVmPub.outparamcount = 0; }
void Scr_EvalExpression(sval_u expr, uint32_t, VariableValue *value) {
    Check(expr.node[0].type == ENUM_primitive_expression);
    Check(expr.node[1].node[0].type == ENUM_integer);
    value->type = VAR_INTEGER; value->u.intValue = expr.node[1].node[1].intValue;
}
void Scr_EvalPrimitiveExpression(sval_u, uint32_t, VariableValue *value) { value->type = VAR_POINTER; value->u.pointerValue = 1; }
scr_entref_t Scr_GetEntityIdRef(uint32_t) { scr_entref_t value{}; value.entnum = 37; value.classnum = 2; return value; }
void FixtureBuiltin() {
    ++callbacks;
    Check(scrVmPub.outparamcount == 2);
    Check(scrVmPub.top[0].u.intValue == 22 && scrVmPub.top[-1].u.intValue == 11);
    Scr_ClearOutParams();
    ++scrVmPub.top; scrVmPub.top->type = VAR_VECTOR; scrVmPub.top->u.vectorValue = payload; scrVmPub.inparamcount = 1;
}
void FixtureMethod(scr_entref_t entity) { Check(entity.entnum == 37); methodClass = entity.classnum; FixtureBuiltin(); }
void (*Scr_GetFunction(const char **, int *))() { return FixtureBuiltin; }
void (*Scr_GetMethod(const char **, int *))(scr_entref_t) { ++methodLookups; return FixtureMethod; }
bool IsObject(VariableValue *value) { return value->type >= VAR_THREAD; }
uint32_t FindArrayVariable(uint32_t, int) { return 0; }
char SetEntityFieldValue(uint32_t, int, int, VariableValue *) { return 0; }
uint32_t GetNewVariable(uint32_t, uint32_t) { return 7; }
bool Sys_IsRemoteDebugClient() { return false; }
void Scr_RemoveValue(Scr_WatchElement_s *element) { element->valueDefined = false; }
void ReplaceString(const char **out, const char *) { *out = "value"; }
void Scr_GetValueString(uint32_t, VariableValue *value, int, char *out) { AddRefToValue(value->type, value->u); out[0] = 0; }
int Com_sprintf(char *out, uint32_t, const char *, ...) { out[0] = 0; return 0; }

#include "script_debugger_slice.inc"

sval_u IntegerExpression(int value) { return debugger_node1(ENUM_primitive_expression, debugger_node1(ENUM_integer, sval_u(value))); }
sval_u ParameterList() { return prepend_node(IntegerExpression(11), linked_list_end(IntegerExpression(22))); }
sval_u FunctionName() { return debugger_node1(ENUM_script_call, debugger_node1(ENUM_function, debugger_node1(ENUM_local_function, sval_u(1)))); }
void TestExpressionCompilation()
{
    sval_u text = debugger_node1(ENUM_string, sval_u(1));
    Scr_CompilePrimitiveExpression(&text);
    HighAddress(text.node); HighAddress(text.node[1].debugString);
    Check(std::strcmp(text.node[1].debugString, "fixture") == 0);
    sval_u variable = debugger_node1(ENUM_local_variable, sval_u(5));
    Scr_CompileVariableExpression(&variable);
    Check(variable.node[1].stringValue == 15);
    sval_u field = debugger_node2(ENUM_field_variable, debugger_node0(ENUM_self), sval_u(8));
    Scr_CompileVariableExpression(&field);
    Check(field.node[1].node[0].type == ENUM_self && field.node[2].stringValue == 18);
    sval_u single = linked_list_end(IntegerExpression(9));
    Scr_CompilePrimitiveExpressionList(&single);
    Check(single.node[0].type == ENUM_primitive_expression);
    sval_u triple = prepend_node(IntegerExpression(1), prepend_node(IntegerExpression(2), linked_list_end(IntegerExpression(3))));
    Scr_CompilePrimitiveExpressionList(&triple);
    Check(triple.node[0].type == ENUM_vector);
    for (int i = 1; i <= 3; ++i) Check(triple.node[i].node[1].node[1].intValue == i);
}
void TestDebuggerBuiltins()
{
    HighAddress(reinterpret_cast<void *>(&FixtureBuiltin)); HighAddress(reinterpret_cast<void *>(&FixtureMethod));
    sval_u call = debugger_node2(ENUM_call, FunctionName(), ParameterList());
    Check(Scr_CompileCallExpression(&call));
    VariableValue value{};
    value.u.vectorValue = nullptr;
    Scr_EvalFunction(call.node[1], call.node[2], 0, &value);
    Check(callbacks == 1 && value.type == VAR_VECTOR && value.u.vectorValue == payload);
    sval_u method = debugger_node3(ENUM_method, debugger_node0(ENUM_self), FunctionName(), ParameterList());
    Check(Scr_CompileCallExpression(&method));
    Scr_EvalMethod(method.node[1], method.node[2], method.node[3], 0, &value);
    Check(callbacks == 2 && methodClass == 2 && value.u.vectorValue == payload);
    Check(scrVmPub.top == scrVmPub.stack && scrVarPub.evaluate && !scrVmPub.debugCode);
    scrVmPub.top->type = VAR_VECTOR; scrVmPub.top->u.vectorValue = payload; scrVmPub.breakpointOutparamcount = 1;
    value.u.vectorValue = nullptr;
    Scr_GetValue(0, &value);
    Check(value.type == VAR_VECTOR && value.u.vectorValue == payload);
}
void TestBuiltinMethodCache()
{
    scrCompilePub.builtinMeth = 900;
    const char *name = "fixture";
    int type = 0;
    const int before = methodLookups;
    const auto first = Scr_GetCachedBuiltinMethod(1, &name, &type);
    const auto second = Scr_GetCachedBuiltinMethod(1, &name, &type);
    Check(first == &FixtureMethod && second == first);
    Check(methodLookups == before + 1 && type == BUILTIN_ANY);
    HighAddress(reinterpret_cast<const void *>(second));
    sval_u function; function.block = reinterpret_cast<scr_block_s *>(second);
    sval_u params = ParameterList(); Scr_CompileCallExpressionList(&params);
    VariableValue value{}; value.u.vectorValue = nullptr;
    Scr_EvalMethod(debugger_node0(ENUM_self), function, params, 0, &value);
    Check(callbacks == 3 && value.u.vectorValue == payload && methodClass == 2);
}
void TestEntityAndWatchTransfers()
{
    VariableValue value{}; value.type = VAR_VECTOR; value.u.vectorValue = payload;
    scrVarGlob.variableList[2].w.type = VAR_ENTITY;
    SetVariableEntityFieldValue(1, 5, &value);
    const auto &entry = scrVarGlob.variableList[VARIABLELIST_CHILD_BEGIN + 7];
    Check((entry.w.type & VAR_MASK) == VAR_VECTOR && entry.u.u.vectorValue == payload);
    Scr_WatchElement_s element{}; element.breakpointType = 1;
    Scr_ScriptWatch watch{};
    Check(watch.PostEvaluateWatchElement(&element, &value));
    Check(element.valueDefined && element.value.u.vectorValue == payload);
    Check(Scr_WatchElementHasSameValue(&element, &value) == 1);
}
int main()
{
    payload = static_cast<float *>(Allocate(3 * sizeof(float)));
    payload[0] = 1; payload[1] = 2; payload[2] = 3;
    scrVarPub.evaluate = true;
    TestExpressionCompilation(); TestDebuggerBuiltins(); TestBuiltinMethodCache(); TestEntityAndWatchTransfers();
    Check(scrVmDebugPub.checkBreakon == 0 && g_breakonExpr == 0);
    Check(scrVmPub.maxstack == scrVmPub.stack + 31);
    for (void *p : allocations) std::free(p);
    std::printf("script debugger: %d checks passed\n", checks);
}
