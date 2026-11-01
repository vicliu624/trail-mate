#pragma once

#include <cerrno>
#include <pthread.h>
#include <sched.h>

namespace platform::linux_runtime
{
struct OneSegCaptureThreadOps
{
    static int getPolicy(int* policy, sched_param* parameter) { return pthread_getschedparam(pthread_self(), policy, parameter); }
    static int setPolicy(int policy, const sched_param* parameter) { return pthread_setschedparam(pthread_self(), policy, parameter); }
    static int getAffinity(cpu_set_t* cpus) { return pthread_getaffinity_np(pthread_self(), sizeof(*cpus), cpus); }
    static int setAffinity(const cpu_set_t* cpus) { return pthread_setaffinity_np(pthread_self(), sizeof(*cpus), cpus); }
};

// Enter only after the IQ consumer exists. Raising its parent earlier would
// make the consumer inherit the capture thread's FIFO policy and CPU binding.
// The UI and decoder threads keep their independent scheduling throughout.
template <typename Ops = OneSegCaptureThreadOps>
class OneSegCaptureThread
{
  public:
    OneSegCaptureThread() = default;
    OneSegCaptureThread(const OneSegCaptureThread&) = delete;
    OneSegCaptureThread& operator=(const OneSegCaptureThread&) = delete;
    ~OneSegCaptureThread() { (void)finish(); }

    int start(int preferred_cpu = -1)
    {
        if (policy_changed_ || affinity_changed_) return EALREADY;
        int error = Ops::getPolicy(&policy_, &parameter_);
        if (error) return error;
        error = Ops::getAffinity(&cpus_);
        if (error) return error;
        int cpu = CPU_SETSIZE - 1;
        while (cpu >= 0 && !CPU_ISSET(cpu, &cpus_)) --cpu;
        if (cpu < 0) return EINVAL;
        if (preferred_cpu >= 0 && preferred_cpu < CPU_SETSIZE && CPU_ISSET(preferred_cpu, &cpus_)) cpu = preferred_cpu;
        cpu_set_t capture;
        CPU_ZERO(&capture);
        CPU_SET(cpu, &capture);
        error = Ops::setAffinity(&capture);
        if (error) return error;
        affinity_changed_ = true;
        sched_param priority{};
        priority.sched_priority = 70;
        error = Ops::setPolicy(SCHED_FIFO, &priority);
        if (error)
        {
            (void)finish();
            return error;
        }
        policy_changed_ = true;
        return 0;
    }

    int finish()
    {
        int error = 0;
        if (policy_changed_)
        {
            error = Ops::setPolicy(policy_, &parameter_);
            if (!error) policy_changed_ = false;
        }
        if (affinity_changed_)
        {
            const int restored = Ops::setAffinity(&cpus_);
            if (!restored) affinity_changed_ = false;
            if (!error) error = restored;
        }
        return error;
    }

  private:
    int policy_ = SCHED_OTHER;
    sched_param parameter_{};
    cpu_set_t cpus_{};
    bool policy_changed_ = false, affinity_changed_ = false;
};
} // namespace platform::linux_runtime
