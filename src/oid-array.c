#include "oid-array.h"
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

