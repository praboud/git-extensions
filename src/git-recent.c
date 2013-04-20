/*
git-recent shows a directory listing inside a git repository, ordered by
commit date.
Copyright (C) 2013  Peter Raboud

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <git2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "tracked.h"

/*
 * CONSTANTS
 */

#define MAX_REPO_PATH_LEN 128
#define MAX_HEX_LEN (40 + 1)

/*
 * DEBUGGING
 */

void git_oid_print(const git_oid *oid) {
    if (oid) {
        char hex[MAX_HEX_LEN];
        git_oid_fmt(hex, oid);
        hex[MAX_HEX_LEN - 1] = '\0';
        printf("%s\n", hex);
    } else {
        printf("deleted\n");
    }
}

/*
 * OID ARRAY HELPERS
 */

int git_oid_array_elem(const git_oid *oid, const git_oid *array, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (git_oid_cmp(array + i, oid) == 0) {
            return 1;
        }
    }
    return 0;
}

void git_oid_array_remove(const git_oid *oid, git_oid *array, int *n) {
    int i, j;
    for (i = 0; i < *n; i++) {
        if (git_oid_cmp(array + i, oid) == 0) {
            for (j = i + 1; j < *n; j++) {
                array[j-1] = array[j];
            }
            (*n)--;
            break;
        }
    }
}

void git_oid_array_add_parents(git_commit *commit, git_oid **array, int *n) {
    int i;
    int parent_count = git_commit_parentcount(commit);
    free(*array);
    *array = malloc(parent_count * sizeof(git_oid));
    for (i = 0; i < parent_count; i++) {
        (*array)[i] = *git_commit_parent_oid(commit, i);
    }
    *n = parent_count;
}



/*
 * CALLBACKS FOR FILE TREE MAP
 */
int set_initial_oid(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit) {
    if (e) {
        p->in_source_control = 1;
        p->oid = *git_tree_entry_id(e);
        p->modifying_commit = *git_commit_id(commit);
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
    }
    return !e;
}

/*
int echo(tracked_path *p, const git_tree_entry *e, git_commit *c) {
        const git_oid *oid = &(p->modifying_commit);
        char hex[MAX_HEX_LEN];
        git_oid_fmt(hex, oid);
        hex[MAX_HEX_LEN-1] = '\0';
        printf("%s\n", hex);
    return 0;
}
*/

int compare_to_past(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit) {
    const git_oid *commit_oid = git_commit_id(commit);
    int found = 0;
    if (!git_oid_array_elem(commit_oid, p->commit_queue,
                            p->commit_queue_length)) {
    } else if (e && git_oid_cmp(&p->oid, git_tree_entry_id(e)) == 0) {
        // file matches what it used to
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
        p->modifying_commit = *commit_oid;
    } else {
        git_oid_array_remove(commit_oid, p->commit_queue,
                             &(p->commit_queue_length));
        found = p->commit_queue_length == 0;
    }
    return found;
}

/*
 * FILE TREE MAPPING
 */


int map_helper(git_repository *repo, git_oid oid,
                tracked_path *file_tree,
                int (*f)(tracked_path*, const git_tree_entry*,
                         git_commit*)) {

    git_commit *commit;
    git_tree *tree;
    int done;
    int err = git_commit_lookup(&commit, repo, &oid);
    if (err) {
        printf("fatal: Bad ref while revwalking\n");
        exit(1);
    }
    err = git_commit_tree(&tree, commit);
    if (err) {
        printf("fatal: Bad tree while revwalking\n");
    }
    done = tracked_path_map(repo, commit, file_tree, tree, f);
    git_tree_free(tree);
    git_commit_free(commit);
    return done;
}

/*
 * GENERAL HELPERS
 */

/*
 * assumed that both paths are absolute
 */
char * relpath(char *refpoint, char *path, char *buf) {
    while (*refpoint && *refpoint++ == *path++);
    if (!*refpoint) {
        strcpy(buf, path);
        return buf;
    } else {
        return NULL;
    }
}

tracked_path* tracked_path_add_path(char *file_path, char *repo_path, char *cwd,
                        tracked_path *file_tree) {
    char buf[PATH_MAX];
    char real_path_buf[PATH_MAX];
    char *real_path = real_path_buf;

    strcpy(buf, cwd);
    strcat(buf, "/");
    strcat(buf, file_path);
    //printf("buf: %s\n", buf);
    if (!realpath(buf, real_path)) {
        printf("fatal: Could not get real path\n");
        exit(1);
    }
    //printf("tracking path %s\n", real_path);
    while (*real_path && *real_path == *repo_path) {
        real_path++;
        repo_path++;
    }
    printf("tracking: %s\n", real_path);

    return tracked_path_insert(file_tree, real_path);
}


int mod_commits_internal(tracked_path ***out, char **paths, int path_count) {
    int i;
    int err;
    char repo_path [MAX_REPO_PATH_LEN];
    char cwd[PATH_MAX];

    tracked_path *file_tree;

    git_repository *repo;

    tracked_path **tracked;

    file_tree = malloc(sizeof(tracked_path));
    tracked_path_init(file_tree, NULL);
    tracked = malloc(100 * sizeof(tracked_path*));

    if (!getcwd(cwd, PATH_MAX)) {
        printf("fatal: Could not get current working directory\n");
        exit(1);
    }

    // get git repo directory
    err = git_repository_discover(repo_path, MAX_REPO_PATH_LEN,
                                  cwd, 1, NULL);
    if (err) {
        printf("fatal: Not in a git repository\n");
        exit(1);
    }

    // open git repo
    err = git_repository_open(&repo, repo_path);

    if (err) {
        printf("fatal: Could not open git repository\n");
        exit(1);
    }

    if (path_count) {
        for (i = 0; i < path_count; i++) {
            tracked[i] = tracked_path_add_path(
                paths[i],
                repo_path,
                cwd,
                file_tree
            );
        }
    } else {
        DIR *d;
        struct dirent *de;
        d = opendir("./");
        if (d) {
            while ((de = readdir(d))) {
                if (de->d_name[0] != '.') {
                    tracked[path_count++] = tracked_path_add_path(
                        de->d_name,
                        repo_path,
                        cwd,
                        file_tree
                    );
                }
            }
            closedir(d);
        } else {
            printf("fatal: Could not open directory\n");
            exit(1);
        }
    }


    // walk history
    // vars
    git_revwalk *history;
    git_oid oid;

    //get_current_git_repository(&repo);

    git_revwalk_new(&history, repo);
    git_revwalk_sorting(history, GIT_SORT_TIME);
    git_revwalk_push_head(history);

    if (git_revwalk_next(&oid, history) == 0) {
        map_helper(repo, oid, file_tree, &set_initial_oid);

        while (git_revwalk_next(&oid, history) == 0) {
            if(map_helper(repo, oid, file_tree, &compare_to_past)) {
                break;
            }
        }
        //tracked_path_map(repo, NULL, file_tree, NULL, &echo);
    }

    *out = tracked;

    // free memory
    git_repository_free(repo);
    git_revwalk_free(history);
    //tracked_path_free(file_tree);

    return path_count;
}

/*
 * MAIN: DEAL WITH IO
 */

int main(int argc, char *argv[]) {
    tracked_path **tracked;
    int i;
    char hex[MAX_HEX_LEN];
    hex[MAX_HEX_LEN - 1] = '\0';

    int count = mod_commits_internal (&tracked, argv+1, argc-1);

    for (i = 0; i < count; i++) {
        if (tracked[i]->in_source_control) {
            git_oid_fmt(hex, &tracked[i]->modifying_commit);
        } else {
            strcpy(hex, "untracked");
        }
        printf("%s\n", hex);
        //tracked_path_free(tracked[i]);
    }
    free(tracked);
    return 0;
}
