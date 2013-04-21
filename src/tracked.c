#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <git2.h>

#include "tracked.h"

void tracked_path_init(tracked_path *path, char *segment) {
    // assumed that name is already set
    path->in_source_control = 0;
    path->followed = 0;
    path->commit_found = 0;
    path->children = NULL;
    path->filled = 0;
    path->capacity = 0;
    path->commit_queue = NULL;
    path->commit_queue_length = 0;
    path->name_segment = segment;
    path->name_full = NULL;

    // modifying_commit and oid are initialized later
    // (namely, by set_initial_oid)
}

void tracked_path_add_name_full(tracked_path *p, char *name) {
    p->name_full = malloc(strlen(name) * sizeof(char));
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

/*
 * Takes a file tree and a git tree object, and a function of type:
 * (Path, git_obj) -> Bool
 *
 * Recursively traverse file tree and git tree simultaenously.
 * Apply function to all paths and tree_objects (paired).
 * Remove the path from the file tree if the function returns true.
 *
 * This function minimizes the amount of traversing the file tree
 * required.
 */
int tracked_path_map(git_repository *repo, git_commit *commit,
                     tracked_path *file_tree, git_tree *git_tree_v,
                     int (*f)(tracked_path*, const git_tree_entry*,
                              git_commit*)
                     ) {

    tracked_path **r = file_tree->children;
    tracked_path **end = r + file_tree->filled;
    int all_commits_found = 1;
    // iterate over every child of the file tree
    while(r < end) {
        char *segment;
        const git_tree_entry *entry;
        if ((*r)->commit_found) {
            ++r;
            continue;
        }
        // loop invariant: w <= r

        // lookup the corresponding git_tree_element
        segment = (*r)->name_segment;

        if (git_tree_v) {
            entry = git_tree_entry_byname(git_tree_v, segment);
        } else {
            entry = NULL;
        }
        // remove whether or not we want to remove this path from the tree

        if ((*r)->filled != 0) {
            // if the path is another file tree, we recurse
            // first, however, we must lookup the tree object in git
            // this is relatively expensive, so we try to avoid if we
            // don't need the tree (and only need the hex of the tree)
            git_tree *subtree;
            if (entry) {
                git_object *obj;
                if (git_tree_entry_to_object(&obj, repo, entry)) {
                    printf("fatal: Could not lookup object\n");
                    exit(1);
                }
                if (git_object_type(obj) == GIT_OBJ_TREE) {
                    subtree = (git_tree*) obj;
                    obj = NULL;
                } else {
                    subtree = NULL;
                    git_object_free(obj);
                }
            } else {
                subtree = NULL;
            }
            (*r)->commit_found = tracked_path_map(repo, commit, *r, subtree, f);
            if (subtree) {
                git_tree_free(subtree);
            }
        }

        if ((*r)->followed) {
            (*r)->commit_found = f((*r), entry, commit);
        }

        all_commits_found &= (*r)->commit_found;
        r++;
    }
    // return true if we have written nothing, meaning this tree
    // is now empty, and should not be returned to
    return all_commits_found;
}

/* structure for internal use */
struct tracked_path_walker {
    tracked_path **children;
    int index;
    int length;
    struct tracked_path_walker *rest;
};

void tracked_path_followed_array(tracked_path* p, tracked_path **a) {
    int i = 0;
    struct tracked_path_walker *q, *t;

    q = NULL;

    while (p) {
        printf("looping\n");
        if (p->followed) {
            *a++ = p;
        }
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
