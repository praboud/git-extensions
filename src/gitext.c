#include <Python.h>
#include <git2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPO_PATH_LEN 128
#define MAX_HEX_LEN (40 + 1)

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
                array[j-i] = array[j];
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


void tracked_path_init(path *path) {
    // assumed that name is already set
    path->is_tree = 0;
    path->tracked = malloc(sizeof(tracked_path));
    //path->tracked->oid = 0;
    //path->tracked->modifying_commit = 0;
    path->tracked->commit_queue = NULL;
    //path->tracked->mod_date = 0;
}

void file_tree_init(path *path) {
    path->is_tree = 1;
    path->children.capacity = 0;
    path->children.filled = 0;
    path->children.array = NULL;
}

path* file_tree_insert_internal(path *file_tree, char *segment) {
    int i;
    if (!(file_tree && file_tree->is_tree)) {
        printf("Tried to insert into a file\n");
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
    new_path->name = segment;

    file_tree->children.array[file_tree->children.filled++] = new_path;

    return new_path;
}

tracked_path *file_tree_insert(path *file_tree, const char *file_path) {
    char *sep;
    char *section;
    int section_length;
    while (sep = strchr(file_path, '/')) {
        section_length = sep - file_path;
        section = malloc(section_length * sizeof(char));
        char *s = section;
        while (file_path < sep) {
            *s++ = *file_path++;
        }
        file_path++;
        file_tree = file_tree_insert_internal(file_tree, section);
        file_tree_init(file_tree);
    }
    section_length = strlen(file_path);

    section = malloc(section_length * sizeof(char));
    char *s = section;
    while (*file_path) {
        *s++ = *file_path++;
    }
    path *p = file_tree_insert_internal(file_tree, section);
    tracked_path_init(p);
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

int echo(tracked_path *p, const git_tree_entry *e, git_commit *c) {
        const git_oid *oid = &(p->modifying_commit);
        char hex[MAX_HEX_LEN];
        git_oid_fmt(hex, oid);
        hex[MAX_HEX_LEN-1] = '\0';
        printf("%s\n", hex);
    return 0;
}
int compare_to_past(tracked_path *p, const git_tree_entry *e,
                    git_commit *commit) {
    const git_oid *commit_oid = git_commit_id(commit);
    if (!git_oid_array_elem(commit_oid, p->commit_queue,
                            p->commit_queue_length)) {
        return 0;
    } else if (e && git_oid_cmp(&p->oid, git_tree_entry_id(e)) == 0) {
        // file matches what it used to
        git_oid_array_add_parents(commit, &p->commit_queue,
                                  &p->commit_queue_length);
        p->modifying_commit = *commit_oid;
        return 0;

    } else {
        git_oid_array_remove(commit_oid, p->commit_queue,
                             &(p->commit_queue_length));
        if (p->commit_queue_length == 0) {
            return 1;
        }
    }
}

/*
 * FILE TREE MAPPING
 */

int map_over_file_tree(git_repository *repo, git_commit *commit,
                       path *file_tree, git_tree *git_tree_v,
                       int (*f)(tracked_path*, const git_tree_entry*,
                                git_commit*)
                       ) {

    if (file_tree->is_tree) {
        path **r = file_tree->children.array;
        path **w = r;
        path **end = r + file_tree->children.filled;
        while(r < end) {
            char *segment = (*r)->name;
            const git_tree_entry *entry;

            if (git_tree_v) {
                entry = git_tree_entry_byname(git_tree_v, segment);
            } else {
                entry = NULL;
            }
            // invariant: w <= r
            int remove;
            if ((*r)->is_tree) {
                git_tree *subtree;
                if (entry) {
                    git_object *obj;
                    if (git_tree_entry_to_object(&obj, repo, entry)) {
                        printf("Could not lookup object");
                        exit(1);
                    }
                    if (git_object_type(obj) == GIT_OBJ_TREE) {
                        subtree = (git_tree*) obj;
                    } else {
                        subtree = NULL;
                        git_object_free(obj);
                    }
                } else {
                    subtree = NULL;
                }
                remove = map_over_file_tree(repo, commit, *r, subtree, f);
            } else {
                remove = f((*r)->tracked, entry, commit);
            }

            if (!remove) {
                *w++ = *r;
            } else {
                (file_tree->children.filled)--;
            }
            r++;
        }
        return w == 0;
    } else {
        printf("Tried to map over tracked file\n");
        exit(1);
        //return f(file_tree->tracked);
    }
}

void get_current_git_repository(git_repository **repo) {
    const char current_directory []= ".";
    char repo_path [MAX_REPO_PATH_LEN];
    int err;

    err = git_repository_discover(repo_path, MAX_REPO_PATH_LEN,
                                  current_directory, 1, NULL);
    
    if (err) {
        printf("Not in a git repository");
        exit(1);
    } 

    err = git_repository_open(repo, repo_path);

    if (err) {
        printf("Could not open git repository");
        exit(1);
    }
}

void map_helper(git_repository *repo, git_oid oid,
                path *file_tree, 
                int (*f)(tracked_path*, const git_tree_entry*,
                         git_commit*)) {

    git_commit *commit;
    git_tree *tree;
    int err = git_commit_lookup(&commit, repo, &oid);
    if (err) {
        printf("Bad ref while revwalking");
        exit(1);
    }
    err = git_commit_tree(&tree, commit);
    map_over_file_tree(repo, commit, file_tree, tree, f);
}

/*
 * PYTHON BOILERPLACE
 */

static PyObject *py_mod_commits(PyObject *self, PyObject *args) {
    int i;
    PyObject *path_list;

    if (!PyArg_ParseTuple(args, "O", &path_list)) {
        return NULL;
    }
    int path_count = PyList_Size(path_list);

    path *file_tree = malloc(sizeof(path));

    // setup file tree
    //int path_count = 2;
    //char *paths[] = {"setup.py", "bin/git-recent"};

    file_tree_init(file_tree);
    tracked_path **tracked = malloc(path_count * sizeof(tracked_path*));

    for (i = 0; i < path_count; i++) {
        PyObject *s = PySequence_GetItem(path_list, i);
        tracked[i] = file_tree_insert(file_tree, PyString_AsString(s));
    }

    // walk history

    // vars
    git_revwalk *history;
    git_oid oid;
    git_repository *repo;

    get_current_git_repository(&repo);

    git_revwalk_new(&history, repo);
    git_revwalk_sorting(history, GIT_SORT_TIME);
    git_revwalk_push_head(history);

    if (git_revwalk_next(&oid, history) == 0) {
        map_helper(repo, oid, file_tree, &set_initial_oid);

        while (git_revwalk_next(&oid, history) == 0) {
            map_helper(repo, oid, file_tree, &compare_to_past);
        }
        map_over_file_tree(repo, NULL, file_tree, NULL, &echo);
    }

    PyObject *out = PyList_New(path_count);
    for (i = 0; i < path_count; i++) {
        char hex_prim[41];
        git_oid_fmt(hex_prim, &tracked[i]->modifying_commit);
        hex_prim[40] = '\0';
        PyObject *hex = PyString_FromString(hex_prim);
        PyList_SetItem(out, i, hex);
    }
    return out;
}

static PyMethodDef GitExtMethods[] = {
    {"mod_commits", py_mod_commits, METH_VARARGS, ""},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC init_gitext(void) {
    (void) Py_InitModule("_gitext", GitExtMethods);
}