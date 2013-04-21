#ifndef __TRACKED_H__
#define __TRACKED_H__

#define CHANGES_FOUND 0
#define NO_CHANGES_FOUND 1
#define UNRECOGNIZED 2

struct tracked_path_t {
    char *name_segment;
    char *name_full;

    struct tracked_path_t **children;
    int capacity;
    int filled;

    git_oid oid;
    git_oid modifying_commit; // (only a candidate if commit found is unset)
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
                              git_commit*)
                     );

void tracked_path_followed_array(tracked_path *tree, tracked_path **out);
void tracked_path_add_name_full(tracked_path *p, char *name);

// ONLY FOR DEBUGGING
void tracked_path_map(tracked_path* p, void (*f)(tracked_path*));
void trace(tracked_path *p);

#endif
