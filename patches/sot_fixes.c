// Fixes for issues that only happen in the recompilation.

#include "patches.h"

#define osSpTaskYield osSpTaskYield_recomp
void osSpTaskYield(void);

// @recomp When an audio task is queued while an RSP task is running, the scheduler asks the running task to yield.
// On the console an audio task always ends before the next one is queued, but here a task can be delayed (e.g. while
// the window is in the background), so the running task can be an audio task: don't yield it (it asserted).
RECOMP_PATCH void Sched_Yield(Scheduler* sc) {
    if (!(sc->curRSPTask->state & OS_SC_YIELD)) {
        if (sc->curRSPTask->list.t.type == M_AUDTASK) {
            return;
        }

        sc->curRSPTask->state |= OS_SC_YIELD;

        // Send yield request
        osSpTaskYield();
    }
}
