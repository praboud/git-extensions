#include <git2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CONSTANTS
 */

#define MAX_REPO_PATH_LEN 128
#define MAX_HEX_LEN (40 + 1)
#define PATH_MAX 256

git_oid BLANK_OID = { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };


/*
 * DATA DEFINITIONS
 */

struct pathT;

typedef struct {
    git_oid oid;
    git_oid modifying_commit;
    git_oid *commit_queue;
    int commit_queue_length;
} tracked_path;

typedef struct {
    struct pathT **array;
    int capacity;
    int filled;
} path_array;

struct pathT {
    char *name;
    int is_tree;
    union {
        path_array children;
        tracked_path *tracked;
    };
};
typedef struct pathT path;


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
    free(*array);
    int parent_count = git_commit_parentcount(commit);
    *array = malloc(parent_count * sizeof(git_oid));
    for (i = 0; i < parent_count; i++) {
        (*array)[i] = *git_commit_parent_oid(commit, i);
    }
    *n = parent_count;
}


void tracked_path_init(path *path, char *segment) {
    // assumed that name is already set
    path->is_tree = 0;
    path->tracked = malloc(sizeof(tracked_path));
    path->tracked->commit_queue = NULL;
    path->name = segment;

    // modifying_commit and oid are initialized later
    // (namely, by set_initial_oid)
}

void file_tree_init(path *path, char *segment) {
    path->is_tree = 1;
    path->children.capacity = 0;
    path->children.filled = 0;
    path->children.array = NULL;
    path->name = segment;
}

path* file_tree_insert_internal(path *file_tree, char *segment,
                                void (*init)(path*, char*)) {
    int i;
    if (!(file_tree && file_tree->is_tree)) {
        printf("fatal: Tried to insert into a file\n");
        exit(1);
    }

    path_array children = file_tree->children;

    for (i = 0; i < file_tree->children.filled; i++) {
        if (strcmp(segment, file_tree->children.array[i]->name) == 0) {
            // if the subtree already exists, we don't need to do much
            free(segment);
            return file_tree->children.array[i];
        }
    }

    if (file_tree->children.capacity == file_tree->children.filled) {
        int new_capacity = file_tree->children.capacity * 2;
        if (new_capacity == 0) {
            new_capacity = 1;
        }

        path **replacement = malloc(new_capacity * sizeof(path*));
        for (i = 0; i < file_tree->children.filled; i++) {
            replacement[i] = file_tree->children.array[i];
        }
        free(file_tree->children.array);
        file_tree->children.array = replacement;
        file_tree->children.capacity = new_capacity;
    }

    path *new_path = malloc(sizeof(path));

    file_tree->children.array[file_tree->children.filled++] = new_path;

    init(new_path, segment);
    return new_path;
}

/*
 * makes ``a`` point to a NULL terminated string n long, containing
 * the first n characters from b
 */
void strcpyn_term(char **a, const char *b, int n) {
    *a = malloc((n + 1) * sizeof(char));
    char *p = *a;
    char *end = p + n;
    while (p < end) *p++ = *b++;
    *p = '\0';
}

tracked_path *file_tree_insert(path *file_tree, const char *file_path) {
    char *sep;
    char *section;
    int section_length;
    while (sep = strchr(file_path, '/')) {
        section_length = sep - file_path;
        strcpyn_term(&section, file_path, section_length);

        file_path = sep + 1;
        file_tree = file_tree_insert_internal(file_tree, section, &file_tree_init);
    }
    section_length = strlen(file_path);
    strcpyn_term(&section, file_path, section_length);

    path *p = file_tree_insert_internal(file_tree, section, &tracked_path_init);
    return p->tracked;
}

/*
 * CALLBACKS FOR FILE TREE MAP
 */
int set_initial_oid(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit) {
    if (e) {
        p->oid = *git_tree_entry_id(e);
        p->modifying_commit = *git_commit_id(commit);
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
        return 0;
    } else {
        return 1;
    }
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

void file_tree_free(path *t) {
    // does not free tracked path, because we usually want to use this
    // after the tree has been freed
    int i;
    if (t) {
        if (t->name) {
            free(t->name);
        }
        if (t->is_tree) {
            if (t->children.array) {
                for (i = 0; i < t->children.filled; i++) {
                    file_tree_free(t->children.array[i]);
                }
                free(t->children.array);
            }
        }
        free(t);
    }
}

void tracked_path_free(tracked_path *t) {
    if (t) {
        free(t->commit_queue);
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
int file_tree_map(git_repository *repo, git_commit *commit,
                       path *file_tree, git_tree *git_tree_v,
                       int (*f)(tracked_path*, const git_tree_entry*,
                                git_commit*)
                       ) {

    if (file_tree->is_tree) {
        path **r = file_tree->children.array;
        path **w = r;
        path **end = r + file_tree->children.filled;
        // iterate over every child of the file tree
        while(r < end) {
            // loop invariant: w <= r

            // lookup the corresponding git_tree_element
            char *segment = (*r)->name;
            const git_tree_entry *entry;

            if (git_tree_v) {
                entry = git_tree_entry_byname(git_tree_v, segment);
            } else {
                entry = NULL;
            }
            // remove whether or not we want to remove this path from the tree
            int remove;

            if ((*r)->is_tree) {
                // if the path is another file tree, we recurse
                // first, however, we must lookup the tree object in git
                // this is relatively expensive, so we try to avoid if we
                // don't need the tree (and only need the hex of the tree)
                git_tree *subtree;
                if (entry) {
                    git_object *obj;
                    if (git_tree_entry_to_object(&obj, repo, entry)) {
                        printf("fatal: Could not lookup object");
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
                remove = file_tree_map(repo, commit, *r, subtree, f);
                if (subtree) {
                    git_tree_free(subtree);
                }
            } else {
                remove = f((*r)->tracked, entry, commit);
            }

            /*if (entry) {
                git_tree_entry_free(entry);
            }*/

            if (remove) {
                // if we want to remove the entry, we free it, and then
                // NULL it that spot in the array may get written over
                // later
                file_tree_free(*r);
                *r = NULL;
                (file_tree->children.filled)--;
            } else {
                // if we don't want to remove it, then we copy the pointer
                // back into the array (unless the copying is redundant)
                if (w != r) *w = *r;
                w++;
            }
            r++;
        }
        // return true if we have written nothing, meaning this tree
        // is now empty, and should not be returned to
        return w == file_tree->children.array;
    } else {
        printf("fatal: Tried to map over tracked file\n");
        exit(1);
        //return f(file_tree->tracked);
    }
}

int map_helper(git_repository *repo, git_oid oid,
                path *file_tree,
                int (*f)(tracked_path*, const git_tree_entry*,
                         git_commit*)) {

    git_commit *commit;
    git_tree *tree;
    int err = git_commit_lookup(&commit, repo, &oid);
    if (err) {
        printf("fatal: Bad ref while revwalking");
        exit(1);
    }
    err = git_commit_tree(&tree, commit);
    if (err) {
        printf("fatal: Bad tree while revwalking");
    }
    int done = file_tree_map(repo, commit, file_tree, tree, f);
    git_tree_free(tree);
    git_commit_free(commit);
    return done;
}

/*
 * GENERAL HELPERS
 */

void get_current_git_repository(git_repository **repo) {
    const char current_directory []= ".";
    int err;


}

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

void file_tree_setup_with_paths(char **paths, int path_count,
                            char *repo_path, char *cwd,
                            path **file_tree, tracked_path ***tracked) {
    char buf[PATH_MAX];
    char real_path[PATH_MAX];
    int i;

    *file_tree = malloc(sizeof(path));
    file_tree_init(*file_tree, NULL);
    *tracked = malloc(path_count * sizeof(tracked_path*));

    for (i = 0; i < path_count; i++) {
        strcpy(buf, cwd);
        strcat(buf, "/");
        strcat(buf, paths[i]);
        //printf("buf: %s\n", buf);
        if (!realpath(buf, real_path)) {
            printf("fatal: Could not get real path\n");
            exit(1);
        }
        //printf("tracking path %s\n", real_path);
        char *p = real_path;
        char *q = repo_path;
        while (*q && *p == *q) {
            p++;
            q++;
        }
        //printf("tracking: %s\n", p);

        (*tracked)[i] = file_tree_insert(*file_tree, p);
    }
}


void mod_commits_internal(tracked_path ***out, char **paths, int path_count) {
    int i;
    int err;
    char repo_path [MAX_REPO_PATH_LEN];
    char current_directory[PATH_MAX];

    path *file_tree;

    git_repository *repo;

    tracked_path **tracked;


    if (!getcwd(current_directory, PATH_MAX)) {
        printf("fatal: Could not get current working directory");
        exit(1);
    }

    // get git repo directory
    err = git_repository_discover(repo_path, MAX_REPO_PATH_LEN,
                                  current_directory, 1, NULL);
    if (err) {
        printf("fatal: Not in a git repository");
        exit(1);
    }

    // open git repo
    err = git_repository_open(&repo, repo_path);

    if (err) {
        printf("fatal: Could not open git repository");
        exit(1);
    }

    file_tree_setup_with_paths(paths, path_count, repo_path, current_directory,
                               &file_tree, &tracked);

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
        //file_tree_map(repo, NULL, file_tree, NULL, &echo);
    }

    *out = tracked;

    // free memory
    git_repository_free(repo);
    git_revwalk_free(history);
    file_tree_free(file_tree);
}

/*
 * MAIN: DEAL WITH IO
 */

int main(int argc, char *argv[]) {
    tracked_path **tracked;
    int i;

    mod_commits_internal (&tracked, argv+1, argc-1);

    for (i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
        git_oid_print(&tracked[i-1]->modifying_commit);
        tracked_path_free(tracked[i-1]);
    }
    free(tracked);
}
