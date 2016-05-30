#ifndef LIST_HELPERS_H
#define LIST_HELPERS_H

typedef bool (*ListMatchFn)(le_sls_Link_t*);
typedef void (*CleanupFn)(le_sls_Link_t*);


size_t ListFilter(
    le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn
);

bool ListRemoveFirstMatch(
    le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn
);

#endif // LIST_HELPERS_H
