#include "legato.h"
#include "list_helpers.h"

static size_t ListRemoveGeneral(
    le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn,
    bool stopOnFirstRemove);


size_t ListFilter( le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn
)
{
    return ListRemoveGeneral(ls, matchFn, cleanupFn, false);
}

bool ListRemoveFirstMatch(
    le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn
)
{
    return (ListRemoveGeneral(ls, matchFn, cleanupFn, true) == 1);
}

static size_t ListRemoveGeneral(
    le_sls_List_t* ls,
    ListMatchFn matchFn,
    CleanupFn cleanupFn,
    bool stopOnFirstRemove
)
{
    size_t numRemoved = 0;
    le_sls_Link_t* prevNode = NULL;
    le_sls_Link_t* currNode = NULL;

    // Remove the head (if it matches) repeatedly until it doesn't match or the list is empty
    for (prevNode = le_sls_Peek(ls);
         prevNode != NULL && matchFn(prevNode);
         prevNode = le_sls_Peek(ls))
    {
        le_sls_Pop(ls);
        cleanupFn(prevNode);
        numRemoved++;
        if (stopOnFirstRemove)
        {
            return numRemoved;
        }
    }

    // Remove non-head elements until the end of the list is reached
    if (prevNode)
    {
        // The list isn't empty
        for (currNode = le_sls_PeekNext(ls, prevNode);
             currNode != NULL;
             prevNode = currNode, currNode = le_sls_PeekNext(ls, prevNode))
        {
            if (matchFn(currNode))
            {
                le_sls_RemoveAfter(ls, prevNode);
                cleanupFn(currNode);
                numRemoved++;
                if (stopOnFirstRemove)
                {
                    return numRemoved;
                }
            }
        }
    }
    else
    {
        // The list is now empty
    }

    return numRemoved;
}
