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

#ifndef __TRACKED_H__
#define __TRACKED_H__

#include <git2.h>
#include "git-recent.h"

#define CHANGES_FOUND 0
#define NO_CHANGES_FOUND 1
#define UNRECOGNIZED 2

#define MAX_OUTPUT_LINE_LEN 128
#define NUM_COLS_OUTPUT 3

struct tracked_path_t {
    char *name_segment;
    char *name_full;

    struct tracked_path_t **children;
    int capacity;
    int filled;

    git_oid oid;
    git_oid modifying_commit; // (only a candidate if commit found is unset)
    git_time modification_time;
    git_oid *commit_queue;
    int commit_queue_length;

    unsigned int in_source_control: 1; // path is tracked
    unsigned int followed: 1; // user has specified this path

    // true iff we have found the point of modification,
    // don't bother checking if this is set
    unsigned int commit_found: 1; 
    unsigned int commit_found_for_children: 1;
};
typedef struct tracked_path_t tracked_path;

tracked_path* tracked_path_insert(tracked_path *parent, const char *file_path);
void tracked_path_free(tracked_path *t);
void tracked_path_init(tracked_path *path, char *segment);
int tracked_path_git_map(git_repository *repo, git_commit *commit,
                     tracked_path *file_tree, git_tree *git_tree_v,
                     int (*f)(tracked_path*, const git_tree_entry*,
                              git_commit*, git_recent_opts*),
                     git_recent_opts *opts);

void tracked_path_followed_array(tracked_path *tree, tracked_path **out);
void tracked_path_add_name_full(tracked_path *p, char *name);
void tracked_path_set_modifying_commit(tracked_path *p, git_commit *c,
                                       git_recent_opts *);
void tracked_path_map_date_cutoff(tracked_path *p, git_time_t date);
void tracked_path_print(char *str, tracked_path *p, git_recent_opts *opts);
int tracked_path_compare(const void *a, const void *b);

// ONLY FOR DEBUGGING
void tracked_path_map(tracked_path* p, void (*f)(tracked_path*, void *), void *arg);
void trace(tracked_path *p);

#endif
