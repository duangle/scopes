#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

char *realpath(const char *path, char resolved_path[PATH_MAX]);

#ifdef __cplusplus
}
#endif
