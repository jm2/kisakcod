#pragma once

namespace command_dispatch
{
template <typename Command, typename NamesEqual>
[[nodiscard]] Command *FindLinkedCommand(
    Command *head, const char *name, const NamesEqual &namesEqual)
{
    if (!name)
        return nullptr;

    for (Command *current = head; current != nullptr; current = current->next)
    {
        if (current->name && namesEqual(name, current->name))
            return current;
    }
    return nullptr;
}
} // namespace command_dispatch
