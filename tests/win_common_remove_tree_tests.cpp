// win_common_remove_tree_tests.cpp: Sys_RemoveDirTree (universal/win_common.cpp)
// on POSIX, NOW row 13. Com_DeletePlayerProfile hands it
// "<fs_basepath>/players/profiles/<name>/"; the POSIX branch used to return
// false without touching the tree. These checks run the engine function over
// real directory trees under ${TMPDIR:-/var/tmp}.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <qcommon/sys_local.h>
#include <universal/com_memory.h>

// win_common.cpp's file listing allocates from the hunk. Nothing here lists
// files, so reaching one of these is itself a failure.
HunkUser *__cdecl Hunk_UserCreate(int, const char *, bool, bool, int)
{
    std::abort();
}

void *Hunk_UserAlloc(HunkUser *, uint32_t, int)
{
    std::abort();
}

void __cdecl Hunk_UserDestroy(HunkUser *)
{
    std::abort();
}

namespace
{
int g_failures = 0;

void Check(const bool condition, const char *stage)
{
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", stage);
    }
}

bool Exists(const std::string &path)
{
    struct stat status{};
    return lstat(path.c_str(), &status) == 0;
}

bool MakeDirectory(const std::string &path)
{
    return mkdir(path.c_str(), 0700) == 0;
}

bool WriteFile(const std::string &path)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool written = write(fd, "kisak", 5) == 5;
    return close(fd) == 0 && written;
}

bool MakeLink(const std::string &target, const std::string &link)
{
    return symlink(target.c_str(), link.c_str()) == 0;
}

// The service rejects symbolic-link ancestors, so the scratch root is the
// resolved path of a fresh mkdtemp directory.
std::string MakeScratch()
{
    const char *const tmp = std::getenv("TMPDIR");
    std::string pattern = std::string(tmp && tmp[0] ? tmp : "/var/tmp")
        + "/kisak-remove-tree-XXXXXX";
    if (!mkdtemp(pattern.data()))
        return {};
    // realpath allocates the result (POSIX.1-2008), so no fixed buffer bounds it.
    char *const resolved = realpath(pattern.c_str(), nullptr);
    if (!resolved)
        return {};
    std::string result(resolved);
    std::free(resolved);
    return result;
}

// Teardown only: FTW_PHYS never follows a link, FTW_DEPTH removes children
// first. It runs whatever the checks above it found.
int RemoveEntry(const char *path, const struct stat *, int, FTW *)
{
    return std::remove(path);
}

void NestedTree(const std::string &scratch)
{
    const std::string profiles = scratch + "/profiles";
    const std::string root = profiles + "/player";
    Check(MakeDirectory(profiles) && MakeDirectory(root)
            && WriteFile(root + "/config_mp.cfg")
            && MakeDirectory(root + "/a") && MakeDirectory(root + "/a/b")
            && MakeDirectory(root + "/a/b/c") && MakeDirectory(root + "/empty")
            && WriteFile(root + "/a/sibling.bin") && WriteFile(root + "/a/b/mid.txt")
            && WriteFile(root + "/a/b/c/deep.txt"),
        "nested tree: fixture");

    // The trailing separator is the form Com_DeletePlayerProfile passes.
    Check(Sys_RemoveDirTree((root + "/").c_str()), "nested tree: returns true");
    Check(!Exists(root), "nested tree: the directory is gone");
    Check(Exists(profiles), "nested tree: the parent survives");

    const std::string plain = scratch + "/plain";
    Check(MakeDirectory(plain) && WriteFile(plain + "/file"), "plain path: fixture");
    Check(Sys_RemoveDirTree(plain.c_str()) && !Exists(plain),
        "plain path: removed without a trailing separator");
}

void Rejections(const std::string &scratch)
{
    Check(!Sys_RemoveDirTree(nullptr), "null path is rejected");
    Check(!Sys_RemoveDirTree(""), "empty path is rejected");
    Check(!Sys_RemoveDirTree((scratch + "/missing").c_str()), "missing path is rejected");

    const std::string file = scratch + "/regular";
    Check(WriteFile(file), "regular file: fixture");
    Check(!Sys_RemoveDirTree(file.c_str()) && Exists(file),
        "regular file: rejected and left in place");
}

void LinksAreNotFollowed(const std::string &scratch)
{
    const std::string outside = scratch + "/outside";
    const std::string kept = outside + "/keep.txt";
    const std::string root = scratch + "/linked";
    Check(MakeDirectory(outside) && WriteFile(kept) && MakeDirectory(root)
            && WriteFile(root + "/file") && MakeLink(outside, root + "/to-directory")
            && MakeLink(kept, root + "/to-file"),
        "links: fixture");

    Check(Sys_RemoveDirTree(root.c_str()) && !Exists(root),
        "links: the tree holding them is removed");
    Check(Exists(kept), "links: their targets survive");

    const std::string leaf = scratch + "/leaf-link";
    Check(MakeLink(outside, leaf), "leaf link: fixture");
    Check(!Sys_RemoveDirTree(leaf.c_str()) && Exists(leaf) && Exists(kept),
        "leaf link: rejected; the link and its target survive");
}
} // namespace

int main()
{
    const std::string scratch = MakeScratch();
    if (scratch.empty())
    {
        std::fprintf(stderr, "FAIL no scratch directory under ${TMPDIR:-/var/tmp}\n");
        return 1;
    }

    NestedTree(scratch);
    Rejections(scratch);
    LinksAreNotFollowed(scratch);

    if (nftw(scratch.c_str(), RemoveEntry, 16, FTW_DEPTH | FTW_PHYS) != 0)
        std::fprintf(stderr, "warning: could not remove %s\n", scratch.c_str());
    if (g_failures == 0)
        std::printf("Sys_RemoveDirTree: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
