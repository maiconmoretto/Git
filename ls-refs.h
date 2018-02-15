#ifndef LS_REFS_H
#define LS_REFS_H

struct repository;
struct argv_array;
extern int ls_refs(struct repository *r, struct argv_array *keys,
		   struct argv_array *args);

#endif /* LS_REFS_H */
