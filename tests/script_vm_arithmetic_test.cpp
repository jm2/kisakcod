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
//   thread-params   a thread started with parameters, from the engine (as a
//                   player callback is) and from script, finds them below
//                   the stack top.
//   locals          branches and loop bodies that create locals in their
//                   own order merge into the enclosing block's order, and
//                   every local keeps its own value.

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
void ThreadParams()
{
    gsc::SetSource("threadparams", R"(worker(x, y)
{
	report(x * 10 + y);
}

main(a, b)
{
	report(a);
	report(b);
	thread worker(a + b, 7);
	worker(b, a);
}
)");
    std::string error;
    GSC_CHECK(gsc::Load("threadparams", &error));
    const std::vector<int> values = gsc::RunMain({3, 4});
    GSC_CHECK((values == std::vector<int>{3, 4, 77, 43}));
    for (int value : values)
        std::fprintf(stderr, "report %d\n", value);
    gsc::Unload();
}
// Branches, loops with breaks and switch cases create the same locals in
// different orders: merging and transferring the blocks lines each block's
// locals up with its parent's or break block's, shifting slots.
void Locals()
{
    gsc::SetSource("locals", R"(pick(flag)
{
	if (flag)
	{
		a = 1;
		b = 2;
	}
	else
	{
		b = 3;
		a = 4;
	}
	return a * 10 + b;
}

reorder(flag)
{
	a = 1;
	b = 2;
	if (flag)
	{
		b = b + 10;
		a = a + 20;
	}
	return a * 100 + b;
}

cand1(n)
{
	x = 0;
	while (1)
	{
		y = n;
		x = x + y;
		if (x > 5)
			break;
	}
	return x;
}

cand2(n)
{
	for (i = 0; i < n; i++)
	{
		if (i == 2)
		{
			q = i;
			break;
		}
		p = i;
	}
	return n;
}

cand3(n)
{
	switch (n)
	{
	case 1:
		u = 1;
		v = 2;
		break;
	default:
		v = 3;
		u = 4;
		break;
	}
	return u + v;
}

loop(n)
{
	total = 0;
	for (i = 0; i < n; i++)
	{
		step = i + 1;
		total = total + step;
	}
	return total;
}

main()
{
	report(pick(1));
	report(pick(0));
	report(loop(4));
	report(reorder(1));
	report(reorder(0));
	report(cand1(2));
	report(cand2(4));
	report(cand3(1));
}
)");
    std::string error;
    GSC_CHECK(gsc::Load("locals", &error));
    const std::vector<int> values = gsc::RunMain();
    GSC_CHECK((values == std::vector<int>{12, 43, 10, 2112, 102, 6, 4, 3}));
    for (int value : values)
        std::fprintf(stderr, "report %d\n", value);
    gsc::Unload();
}
}  // namespace

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "";
    if (!std::strcmp(which, "arithmetic"))
        Arithmetic();
    else if (!std::strcmp(which, "animtree-limit"))
        Animtrees();
    else if (!std::strcmp(which, "thread-params"))
        ThreadParams();
    else if (!std::strcmp(which, "locals"))
        Locals();
    else
    {
        std::fprintf(stderr, "usage: %s arithmetic|animtree-limit|thread-params|locals\n", argv[0]);
        return 2;
    }
    std::printf("%s: %d failure(s)\n", which, gsc_failures);
    return gsc_failures ? 1 : 0;
}
