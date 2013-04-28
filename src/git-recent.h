#ifndef __GIT_RECENT_H__
#define __GIT_RECENT_H__

#include <git2.h>

#define MAX_HEX_LEN (40 + 1)

typedef struct git_recent_opts {
    git_time_t commit_time_cutoff;
    git_time_t author_time_cutoff;
    unsigned int commit_count_cutoff;
    enum {
        AUTHOR_TIME,
        COMMIT_TIME
    } time_type;
    char **argv;
    int argc;
} git_recent_opts;

#endif
