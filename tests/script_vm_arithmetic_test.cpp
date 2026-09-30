// script_vm_arithmetic_test.cpp: GSC VM defects (NOW row 20, #199). Real
// GSC source goes through the production compiler and VM
// (script_engine_harness.hpp); run as
//   kisakcod-script-vm-arithmetic-tests <case>
//
//   arithmetic      integer % and the shift operators on script values:
//                   INT_MIN % -1 is 0 rather than a divide trap, and shift
//                   counts are taken mod 32 (x86 semantics), not undefined.
//   animtree-limit  the 127th distinct #using_animtree loads; the 128th
//                   fails the load instead of writing past the lookup table.

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include "script_engine_harness.hpp"

namespace
{
int gsc_failures = 0;

void Arithmetic()
{
    // The operands pass through parameters, so the VM evaluates each operator.
    gsc::SetSource("arithmetic", R"(mod(a, b)
{
	return a % b;
}

shl(a, b)
{
	return a << b;
}

shr(a, b)
{
	return a >> b;
}

main()
{
	intMin = -2147483647 - 1;
	report(mod(intMin, -1));
	report(mod(7, -1));
	report(mod(-7, 3));
	report(mod(intMin, 7));
	report(shl(1, 31));
	report(shl(1, 32));
	report(shl(3, 33));
	report(shl(1, -1));
	report(shl(-1, 4));
	report(shr(-8, 1));
	report(shr(-8, 32));
	report(shr(1024, 42));
	report(shr(-1, -1));
	report(shr(intMin, 31));
}
)");
    std::string error;
    GSC_CHECK(gsc::Load("arithmetic", &error));
    const std::vector<int> expected = {0, 0, -1, INT_MIN % 7, INT_MIN, 1, 6, INT_MIN, -16, -4, -8, 1, -1, -1};
    const std::vector<int> values = gsc::RunMain();
    GSC_CHECK(values == expected);
    for (size_t i = 0; i < values.size() && i < expected.size(); ++i)
        if (values[i] != expected[i])
            std::fprintf(stderr, "report %zu: got %d, want %d\n", i, values[i], expected[i]);
    gsc::Unload();
}

std::string UsingAnimtrees(int count)
{
    std::string text;
    for (int i = 1; i <= count; ++i)
        text += "#using_animtree(\"tree" + std::to_string(i) + "\");\n";
    return text + "\nmain()\n{\n\treport(" + std::to_string(count) + ");\n}\n";
}

void Animtrees()
{
    std::string error;
    gsc::SetSource("trees127", UsingAnimtrees(127));
    GSC_CHECK(gsc::Load("trees127", &error));
    GSC_CHECK(gsc::RunMain() == std::vector<int>{127});
    gsc::Unload();

    gsc::SetSource("trees128", UsingAnimtrees(128));
    GSC_CHECK(!gsc::Load("trees128", &error));
    GSC_CHECK(error.find("MAX_XANIMTREE_NUM exceeded") != std::string::npos);
}
}  // namespace

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "";
    if (!std::strcmp(which, "arithmetic"))
        Arithmetic();
    else if (!std::strcmp(which, "animtree-limit"))
        Animtrees();
    else
    {
        std::fprintf(stderr, "usage: %s arithmetic|animtree-limit\n", argv[0]);
        return 2;
    }
    std::printf("%s: %d failure(s)\n", which, gsc_failures);
    return gsc_failures ? 1 : 0;
}
