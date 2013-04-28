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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tracked.h"
#include "oid-array.h"
#include "git-recent.h"

/* constants only used by tracked.c */
#define TIME_STR_MAX_LENGTH 80


// globals used by function pointers called with tracked_path_map
// required because C has no closures
tracked_path **followed_array_global = NULL;
git_commit *modifying_commit_global;

/* structure for internal use */
struct tracked_path_walker {
    tracked_path **children;
    int index;
    int length;
    struct tracked_path_walker *rest;
};

// utility functions

// apply a function pointer onto every element of a tree
// the function pointer must do all work with side effects
void tracked_path_map(tracked_path* p, void (*f)(tracked_path*)) {
    struct tracked_path_walker *q, *t;

    q = NULL;

    while (p) {
        f(p);
        if (p->filled) {
            if (p->filled > 1) {
                t = malloc(sizeof(struct tracked_path_walker));
                t->index = 1;
                t->length = p->filled;
                t->children = p->children;
                t->rest = q;
                q = t;
            }
            p = p->children[0];
        } else if (q){
            p = q->children[q->index++];
            if (q->index == q->length) {
                t = q->rest;
                free(q);
                q = t;
            }
        } else {
            p = NULL;
        }
    }
}


void tracked_path_init(tracked_path *path, char *segment) {
    // assumed that name is already set
    path->followed = 0;
    path->children = NULL;
    path->filled = 0;
    path->capacity = 0;
    path->commit_queue = NULL;
    path->commit_queue_length = 0;
    path->name_segment = segment;
    path->name_full = NULL;

    path->commit_found = path->commit_found_for_children = 0;

    // some initialization is expected to be done by set_initial_oid
    // oid, modifying_commit, in_source_control are
    // intentionally not initialized
}

void tracked_path_add_name_full(tracked_path *p, char *name) {
    p->name_full = malloc((strlen(name) + 1) * sizeof(char));
    strcpy(p->name_full, name);
}

void tracked_path_add_child(tracked_path *parent, tracked_path *child) {
    if (parent->capacity == parent->filled) {
        int new_capacity;
        if (parent->children) {
            new_capacity = parent->capacity * 2;
            parent->children = realloc(parent->children, new_capacity * sizeof(tracked_path*));
        } else {
            new_capacity = 1;
            parent->children = malloc(new_capacity * sizeof(tracked_path*));
        }
        parent->capacity = new_capacity;
    }
    parent->children[parent->filled++] = child;
}

tracked_path* tracked_path_insert_internal(tracked_path *parent, char *segment) {
    int i;
    tracked_path *new_path;
    for (i = 0; i < parent->filled; i++) {
        if (strcmp(segment, parent->children[i]->name_segment) == 0) {
            // if the subtree already exists, we don't need to do much
            free(segment);
            return parent->children[i];
        }
    }

    new_path = malloc(sizeof(tracked_path));
    tracked_path_init(new_path, segment);
    tracked_path_add_child(parent, new_path);
    return new_path;
}

/*
 * makes ``a`` point to a NULL terminated string n long, containing
 * the first n characters from b
 */
void strcpyn_term(char **a, const char *b, int n) {
    char *p, *end;
    *a = malloc((n + 1) * sizeof(char));
    p = *a;
    end = p + n;
    while (p < end) *p++ = *b++;
    *p = '\0';
}

tracked_path* tracked_path_insert(tracked_path *parent, const char *path) {
    char *sep;
    char *section;
    int section_length;
    tracked_path *p;
    while ((sep = strchr(path, '/'))) {
        section_length = sep - path;
        strcpyn_term(&section, path, section_length);

        path = sep + 1;
        parent = tracked_path_insert_internal(parent, section);
    }
    section_length = strlen(path);
    strcpyn_term(&section, path, section_length);

    p = tracked_path_insert_internal(parent, section);
    p->followed = 1;
    return p;
}

void tracked_path_free(tracked_path *t) {
    // does not free tracked path, because we usually want to use this
    // after the tree has been freed
    int i;
    if (t) {
        if (t->name_segment) free(t->name_segment);
        if (t->name_full) free(t->name_full);
        if (t->children) {
            for (i = 0; i < t->filled; i++) {
                tracked_path_free(t->children[i]);
            }
            free(t->children);
        }
        if (t->commit_queue) free(t->commit_queue);
        free(t);
    }
}

void modifying_commit_mapper(tracked_path *p) {
    if (p->in_source_control && !p->commit_found) {
        p->modifying_commit = *git_commit_id(modifying_commit_global);
        git_oid_array_add_parents(modifying_commit_global, &p->commit_queue,
                                  &p->commit_queue_length);
    }
}

void trace(tracked_path *p) {
    printf("entry %20s, followed=%d, isc=%d, cf=%d, cffc=%d\n",
           p->name_segment,
           p->followed,
           p->in_source_control,
           p->commit_found,
           p->commit_found_for_children);
}

/*
 * Takes a file tree and a git tree object, and a function of type:
 * (Path, git_obj) -> Bool
 *
 * Recursively traverse file tree and git tree simultaenously.
 * Apply function to all paths and tree_objects (paired).
 * Don't recurse into a path's subtree if the callback returns non-zero
 *
 * This function minimizes the amount of traversing the file tree
 * required.
 */
int tracked_path_git_map(git_repository *repo, git_commit *commit,
                         tracked_path *file_tree, git_tree *git_tree_v,
                         int (*f)(tracked_path*, const git_tree_entry*,
                                  git_commit*)
                         ) {

    tracked_path *p;
    int all_commits_found = 1;
    int i;
    int len = file_tree->filled;
    // iterate over every child of the file tree
    for (i = 0; i < len; i++) {
        p = file_tree->children[i];
        char *segment;
        int skip;
        const git_tree_entry *entry;
        if (p->commit_found && p->commit_found_for_children) {
            continue;
        }

        // lookup the corresponding git_tree_element
        segment = p->name_segment;

        if (git_tree_v) {
            entry = git_tree_entry_byname(git_tree_v, segment);
        } else {
            entry = NULL;
        }

        // apply callback, which does some work via side effects to the
        // tracked_path, and also tells us whether to descend into its
        // children
        skip = f(p, entry, commit);
        if (skip == NO_CHANGES_FOUND && p->filled != 0) {
            printf("aborting descent at %s\n", p->name_segment);
            modifying_commit_global = commit;
            tracked_path_map(p, &modifying_commit_mapper);
        }

        if (!skip && !p->commit_found_for_children) {
            // if the path is another file tree, we recurse
            // first, however, we must lookup the tree object in git
            // this is relatively expensive, so we try to avoid if we
            // don't need the tree (and only need the hex of the tree)
            git_tree *subtree;
            if (entry) {
                if (git_tree_lookup(&subtree, repo,
                                    git_tree_entry_id(entry))) {
                    printf("fatal: Could not lookup object\n");
                    exit(1);
                }
            } else {
                subtree = NULL;
            }
            tracked_path_git_map(repo, commit, p, subtree, f);
            if (subtree) {
                git_tree_free(subtree);
            }
        }

        all_commits_found &= p->commit_found && p->commit_found_for_children;
    }
    // return true if we have written nothing, meaning this tree
    // is now empty, and should not be returned to
    file_tree->commit_found_for_children = all_commits_found;
    return all_commits_found;
}

void followed_array_mapper(tracked_path *tree) {
    if (tree->followed) *followed_array_global++ = tree;
}

void tracked_path_followed_array(tracked_path *tree, tracked_path **out) {
    followed_array_global = out;
    tracked_path_map(tree, &followed_array_mapper);
}

void tracked_path_set_modifying_commit(tracked_path *p, git_commit *commit) {
    const git_signature *author = git_commit_author(commit);
    p->modifying_commit = *git_commit_id(commit);
    p->modification_time = author->when;
}

void tracked_path_print(tracked_path *p) {
    char hex[MAX_HEX_LEN];
    char time_str[TIME_STR_MAX_LENGTH];

    hex[MAX_HEX_LEN - 1] = '\0';

    if (p->in_source_control) {
        git_oid_fmt(hex, &p->modifying_commit);
        git_time mod_time = p->modification_time;
        git_time_t timestamp = mod_time.time + 60 * mod_time.offset;
        strftime(time_str, TIME_STR_MAX_LENGTH, "%c", gmtime(&timestamp));
        int offset = mod_time.offset % 60 + (mod_time.offset / 60) * 100;
        printf("%s %s %s %+.4d\n", p->name_full, hex, time_str, offset);
    } else {
        printf("%s untracked\n", p->name_full);
    }
}

int tracked_path_compare(const void *a, const void *b) {
    const tracked_path *ap, *bp;
    ap = *(tracked_path**)a;
    bp = *(tracked_path**)b;
    git_time_t at, bt;
    if (!ap->in_source_control) return 1;
    else if (!bp->in_source_control) return -1;
    else {
        at = ap->modification_time.time + 60 * ap->modification_time.offset;
        bt = bp->modification_time.time + 60 * bp->modification_time.offset;
        return bt - at;
    }
}
