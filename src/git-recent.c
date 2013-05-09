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

/* needed to use strptime */
#define _XOPEN_SOURCE 600

#include <git2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <argp.h>

#include <time.h>

#include "git-recent.h"
#include "tracked.h"
#include "oid-array.h"

/*
 * CONSTANTS
 */

#define MAX_REPO_PATH_LEN 128

/*
 * CALLBACKS FOR FILE TREE MAP
 */
int set_initial_oid(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit, git_recent_opts *opts) {
    if (e) {
        p->in_source_control = 1;
        p->commit_found = p->commit_found_for_children = 0;
        p->oid = *git_tree_entry_id(e);
        tracked_path_set_modifying_commit(p, commit, opts);
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
        if (p->filled == 0) p->commit_found_for_children = 1;
    } else {
        p->in_source_control = 0;
        p->commit_found = p->commit_found_for_children = 1;
    }
    return p->commit_found;
}


int compare_to_past(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit, git_recent_opts *opts) {
    const git_oid *commit_oid = git_commit_id(commit);
    if (!git_oid_array_elem(commit_oid, p->commit_queue,
                            p->commit_queue_length)) {
        return UNRECOGNIZED;
    } else if (e && git_oid_cmp(&p->oid, git_tree_entry_id(e)) == 0) {
        // file matches what it used to
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);

        // we only change the tentative commit if we have not found
        // the actual commit yet
        // we may still want to run this function on a path (specifically a directory)
        // where we have found the modifying commit in order to determine if
        // the contents of the directory have changed, and if we should look at them
        // item by item
        if (!p->commit_found) tracked_path_set_modifying_commit(p, commit, opts);
        return NO_CHANGES_FOUND;
    } else {
        git_oid_array_remove(commit_oid, p->commit_queue,
                             &(p->commit_queue_length));
        p->commit_found = p->commit_queue_length == 0;
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
        return CHANGES_FOUND;
    }
}

/*
 * FILE TREE MAPPING
 */

int map_over_commit(git_repository *repo, git_oid oid,
                    tracked_path *file_tree,
                    int (*f)(tracked_path*, const git_tree_entry*,
                             git_commit*, git_recent_opts*),
                    unsigned int commit_count, git_recent_opts *opts) {

    git_commit *commit;
    git_tree *tree;
    const git_signature *sig;
    git_time_t cutoff = 0;
    int done;
    if (opts->commit_count_cutoff && commit_count > opts->commit_count_cutoff) {
        /* we have no meaningful date to set here */
        return 1;
    }
    int err = git_commit_lookup(&commit, repo, &oid);
    if (err) {
        printf("fatal: Bad ref while revwalking\n");
        exit(1);
    }
    if (opts->author_time_cutoff) {
        sig = git_commit_author(commit);
        if (sig->when.time < opts->author_time_cutoff) {
            tracked_path_map_date_cutoff(file_tree, opts->author_time_cutoff);
            return 1;
        }
    }
    if (!cutoff && opts->commit_time_cutoff) {
        sig = git_commit_committer(commit);
        if (sig->when.time < opts->commit_time_cutoff) {
            tracked_path_map_date_cutoff(file_tree, opts->commit_time_cutoff);
            return 1;
        }
    }

    err = git_commit_tree(&tree, commit);
    if (err) {
        printf("fatal: Bad tree while revwalking\n");
    }
    done = tracked_path_git_map(repo, commit, file_tree, tree, f, opts);
    git_tree_free(tree);
    git_commit_free(commit);
    return done;
}

/*
 * GENERAL HELPERS
 */

/*
 * Takes a path relative to the current directory, works out the path relative
 * to the git repo, and adds it to the tree of paths to be tracked
 */
tracked_path* track_rel_path(char *file_path, char *repo_path,
                             char *cwd, tracked_path *file_tree) {
    char real_path_buf[PATH_MAX];
    char *real_path = real_path_buf;

    /* get path relative to root */
    strcpy(real_path, cwd);
    strcat(real_path, "/");
    strcat(real_path, file_path);
    /* get path relative to git-repo */
    while (*real_path && *real_path == *repo_path) {
        real_path++;
        repo_path++;
    }
    /* insert it into tree */
    return tracked_path_insert(file_tree, real_path);
}


int find_modifying_commits(tracked_path **out,
                                      git_recent_opts *opts) {
    int i;
    int err;
    char repo_path [MAX_REPO_PATH_LEN];
    char cwd[PATH_MAX];
    int path_count = opts->argc;

    tracked_path *file_tree;
    tracked_path *t;

    git_repository *repo;

    file_tree = malloc(sizeof(tracked_path));
    tracked_path_init(file_tree, NULL);

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

    // setup file tree
    if (path_count) {
        for (i = 0; i < path_count; i++) {
            t = track_rel_path(
                opts->argv[i],
                repo_path,
                cwd,
                file_tree
            );
            tracked_path_add_name_full(t, opts->argv[i]);
        }
    } else {
        DIR *d;
        struct dirent *de;
        d = opendir("./");
        if (d) {
            while ((de = readdir(d))) {
                if (de->d_name[0] != '.') {
                    t = track_rel_path(
                        de->d_name,
                        repo_path,
                        cwd,
                        file_tree
                    );
                    path_count++;
                    tracked_path_add_name_full(t, de->d_name);
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

    /* we count from one in this case, because the argument is taken as such */
    unsigned int commit_count = 1;

    //get_current_git_repository(&repo);

    git_revwalk_new(&history, repo);
    git_revwalk_sorting(history, GIT_SORT_TIME);
    git_revwalk_push_head(history);

    if (git_revwalk_next(&oid, history) == 0) {
        map_over_commit(repo, oid, file_tree, &set_initial_oid, commit_count,
                       opts);
        commit_count++;
        while (git_revwalk_next(&oid, history) == 0) {
            if(map_over_commit(repo, oid, file_tree, &compare_to_past,
                              commit_count, opts)) {
                break;
            }
            commit_count++;
        }
    }

    *out = file_tree;

    // free memory
    git_repository_free(repo);
    git_revwalk_free(history);

    return path_count;
}

/*
 * Argument parsing
 */

const char *argp_program_version = "git-recent 0.2";
const char *argp_program_bug_address = "<praboud@gmail.com>";

/* Program documentation. */
static char argp_doc[] =
"git-recent - list files in source control by last modification date";

/* A description of the arguments we accept. */
static char argp_args_doc[] = "[PATH ...]";

static struct argp_option argp_optspec[] = {
    {"author", 'a', 0, 0, "Order by author date", 0},
    {"commit", 'c', 0, 0, "Order by commit date", 0},
    {"after", 'A', "TIME", 0, "Ignore commits whose author date is before <TIME>", 0},
    //{"after-author", 'A', "TIME", OPTION_ALIAS, "Ignore changes made before <TIME>", 0},
    {"after-commit", 'C', "TIME", 0, "Ignore commits whose commit date is before <TIME>", 0},
    {"commits-ago", 'g', "N", 0, "Ignore commits more than <N> commits ago", 0},
    {0}
};

int argp_parse_date(char *arg, git_time_t *time) {
    struct tm t;
    char *success;
    memset(&t, 0, sizeof(struct tm));
    // try multiple formatting options
    if ((success = strptime(arg, "%Y-%m-%d %H-%m-%S", &t))){}
    else if ((success = strptime(arg, "%Y-%m-%d", &t))){}

    if (success) {
        *time = mktime(&t);
        return 0;
    } else {
        return ARGP_ERR_UNKNOWN;
    }
}

static error_t argp_parse_callback(int key, char *arg, struct argp_state *state) {
    git_recent_opts *a = state->input;
    switch (key) {
    case 'a':
        a->time_type = AUTHOR_TIME;
        break;
    case 'c':
        a->time_type = COMMIT_TIME;
        break;
    case 'A':
        return argp_parse_date(arg, &a->author_time_cutoff);
    case 'C':
        return argp_parse_date(arg, &a->commit_time_cutoff);
    case 'g':
        a->commit_count_cutoff = atoi(arg);
        break;
    case ARGP_KEY_ARGS:
        a->argv = state->argv + state->next;
        a->argc = state->argc - state->next;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { argp_optspec, argp_parse_callback, argp_doc, argp_args_doc, 0, 0, 0 };

void git_recent_opts_default(git_recent_opts *opts) {
    opts->commit_time_cutoff = 0;
    opts->author_time_cutoff = 0;
    opts->commit_count_cutoff = 0;
    opts->time_type = AUTHOR_TIME;
    opts->argv = NULL;
    opts->argc = 0;
}


/*
 * MAIN: DEAL WITH IO
 */

int main(int argc, char *argv[]) {
    tracked_path **tracked;
    tracked_path *tree;

    git_recent_opts opts;
    git_recent_opts_default(&opts);
    argp_parse(&argp, argc, argv, 0, 0, &opts);
    printf("%d files passed\n", opts.argc);

    int i;
    int path_count;

    path_count = find_modifying_commits(&tree, &opts);
    printf("%d files listed\n", path_count);

    tracked = malloc(path_count * sizeof(tracked_path*));
    tracked_path_followed_array(tree, tracked);

    qsort(tracked, path_count, sizeof(tracked_path*), tracked_path_compare);

    for (i = 0; i < path_count; i++) {
        tracked_path_print(tracked[i]);
    }

    tracked_path_free(tree);
    free(tracked);
    return 0;
}
