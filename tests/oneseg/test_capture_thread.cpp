#include "platform/linux/oneseg_capture_thread.h"
#include <cassert>
#include <initializer_list>

using namespace platform::linux_runtime;
struct Ops
{
    static inline int policy = SCHED_OTHER, priority = 0;
    static inline cpu_set_t cpus;
    static inline int policy_calls = 0, affinity_calls = 0;
    static inline int fail_policy_at = 0, fail_affinity_at = 0;
    static void reset()
    {
        policy = SCHED_OTHER;
        priority = policy_calls = affinity_calls = fail_policy_at = fail_affinity_at = 0;
        CPU_ZERO(&cpus);
        CPU_SET(0, &cpus);
        CPU_SET(3, &cpus);
    }
    static int getPolicy(int* value, sched_param* parameter)
    {
        *value = policy;
        parameter->sched_priority = priority;
        return 0;
    }
    static int setPolicy(int value, const sched_param* parameter)
    {
        if (++policy_calls == fail_policy_at) return EPERM;
        policy = value;
        priority = parameter->sched_priority;
        return 0;
    }
    static int getAffinity(cpu_set_t* value)
    {
        *value = cpus;
        return 0;
    }
    static int setAffinity(const cpu_set_t* value)
    {
        if (++affinity_calls == fail_affinity_at) return EINVAL;
        cpus = *value;
        return 0;
    }
};

int main()
{
    Ops::reset();
    CPU_SET(2, &Ops::cpus);
    {
        OneSegCaptureThread<Ops> guard;
        assert(!guard.start(2) && CPU_COUNT(&Ops::cpus) == 1 && CPU_ISSET(2, &Ops::cpus));
    }
    assert(CPU_COUNT(&Ops::cpus) == 3);
    Ops::reset();
    {
        OneSegCaptureThread<Ops> guard;
        assert(!guard.start());
        assert(Ops::policy == SCHED_FIFO && Ops::priority == 70 && CPU_COUNT(&Ops::cpus) == 1 && CPU_ISSET(3, &Ops::cpus));
        assert(guard.start() == EALREADY);
        assert(!guard.finish() && !guard.finish());
        assert(Ops::policy == SCHED_OTHER && Ops::priority == 0 && CPU_COUNT(&Ops::cpus) == 2);
    }
    for (bool policy_failure : {false, true})
    {
        Ops::reset();
        Ops::fail_policy_at = policy_failure ? 1 : 0;
        Ops::fail_affinity_at = policy_failure ? 0 : 1;
        OneSegCaptureThread<Ops> guard;
        assert(guard.start() != 0);
        assert(Ops::policy == SCHED_OTHER && CPU_COUNT(&Ops::cpus) == 2);
    }
    for (bool policy_failure : {false, true})
    {
        Ops::reset();
        {
            OneSegCaptureThread<Ops> guard;
            assert(!guard.start());
            Ops::fail_policy_at = policy_failure ? 2 : 0;
            Ops::fail_affinity_at = policy_failure ? 0 : 2;
            assert(guard.finish() != 0);
        } // Destructor retries a failed restore.
        assert(Ops::policy == SCHED_OTHER && Ops::priority == 0 && CPU_COUNT(&Ops::cpus) == 2);
    }
}
