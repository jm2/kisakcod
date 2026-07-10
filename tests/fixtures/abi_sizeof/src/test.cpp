// This is scanner input, not a translation unit. Keep constructs that exercise
// CMake's list parser and the assertion statement collector.
const char *rangeMessage = "value is in [0, max";
const char *commentMarker = "/* static_assert(sizeof(StringOnly) == 64); */";

#define iassert(expression)

static_assert(
    sizeof(Multiline) == 12
);
static_assert(sizeof(First) == 4 && sizeof(Second) == 8);
iassert(sizeof(Runtime) == 16);
static_assert(sizeof(Formula) * 2 == 24);

/*
static_assert(sizeof(CommentedOut) == 32);
*/
