#include <qcommon/cmd_dispatch.h>

#include <cstdio>
#include <cstring>

namespace
{
struct Command final
{
    Command *next = nullptr;
    const char *name = nullptr;
    void (*function)() = nullptr;
};

int failures = 0;

void Check(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

bool NamesEqual(const char *lhs, const char *rhs)
{
    return std::strcmp(lhs, rhs) == 0;
}
} // namespace

int main()
{
    Check(
        command_dispatch::FindLinkedCommand<Command>(
            nullptr, "missing", NamesEqual) == nullptr,
        "an empty registry is safe");
    Check(
        command_dispatch::FindLinkedCommand<Command>(
            nullptr, nullptr, NamesEqual) == nullptr,
        "a null lookup name is rejected");

    Command tail{nullptr, "tail", nullptr};
    Check(
        command_dispatch::FindLinkedCommand(&tail, "tail", NamesEqual)
            == &tail,
        "a single tail node is dispatched");

    Command middle{&tail, "middle", nullptr};
    Command head{&middle, "head", nullptr};
    Check(
        command_dispatch::FindLinkedCommand(&head, "head", NamesEqual)
            == &head,
        "the head is found");
    Check(
        command_dispatch::FindLinkedCommand(&head, "middle", NamesEqual)
            == &middle,
        "a middle node is found");
    Check(
        command_dispatch::FindLinkedCommand(&head, "tail", NamesEqual)
            == &tail,
        "the final node is not skipped");
    Check(
        command_dispatch::FindLinkedCommand(&head, "missing", NamesEqual)
            == nullptr,
        "a missing command returns null");
    Check(
        command_dispatch::FindLinkedCommand(&head, nullptr, NamesEqual)
            == nullptr,
        "a null lookup name never reaches the comparator");

    Command afterNullName{nullptr, "after-null", nullptr};
    Command nullName{&afterNullName, nullptr, nullptr};
    Check(
        command_dispatch::FindLinkedCommand(
            &nullName, "after-null", NamesEqual) == &afterNullName,
        "a null registry name is skipped safely");

    Command duplicateTail{nullptr, "duplicate", nullptr};
    Command duplicateHead{&duplicateTail, "duplicate", nullptr};
    Check(
        command_dispatch::FindLinkedCommand(
            &duplicateHead, "duplicate", NamesEqual) == &duplicateHead,
        "the first matching command retains precedence");

    return failures == 0 ? 0 : 1;
}
