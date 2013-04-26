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
        (*array)[i] = *git_commit_parent_id(commit, i);
    }
    *n = parent_count;
}

