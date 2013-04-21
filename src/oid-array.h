#ifndef __OID_ARRAY_H__
#define __OID_ARRAY_H__

#include <git2.h>

int git_oid_array_elem(const git_oid *oid, const git_oid *array, int n);
void git_oid_array_remove(const git_oid *oid, git_oid *array, int *n);
void git_oid_array_add_parents(git_commit *commit, git_oid **array, int *n);

#endif
