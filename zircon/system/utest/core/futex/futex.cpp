// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/time.h>
#include <zircon/types.h>

static bool TestFutexWaitValueMismatch() {
    BEGIN_TEST;
    int32_t futex_value = 123;
    zx_status_t rc = zx_futex_wait(&futex_value, futex_value + 1,
                                         ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    ASSERT_EQ(rc, ZX_ERR_BAD_STATE, "Futex wait should have reurned bad state");
    END_TEST;
}

static bool TestFutexWaitTimeout() {
    BEGIN_TEST;
    int32_t futex_value = 123;
    zx_status_t rc = zx_futex_wait(&futex_value, futex_value, ZX_HANDLE_INVALID, 0);
    ASSERT_EQ(rc, ZX_ERR_TIMED_OUT, "Futex wait should have reurned timeout");
    END_TEST;
}

// This test checks that the timeout in futex_wait() is respected
static bool TestFutexWaitTimeoutElapsed() {
    BEGIN_TEST;
    int32_t futex_value = 0;
    constexpr zx_duration_t kRelativeDeadline = ZX_MSEC(500);
    for (int i = 0; i < 5; ++i) {
        zx_time_t now = zx_clock_get_monotonic();
        zx_status_t rc = zx_futex_wait(&futex_value, 0, ZX_HANDLE_INVALID,
                                       zx_deadline_after(kRelativeDeadline));
        ASSERT_EQ(rc, ZX_ERR_TIMED_OUT, "wait should time out");
        zx_duration_t elapsed = zx_time_sub_time(zx_clock_get_monotonic(), now);
        if (elapsed < kRelativeDeadline) {
            unittest_printf("\nelapsed %" PRIu64
                            " < kRelativeDeadline: %" PRIu64 "\n",
                            elapsed, kRelativeDeadline);
            EXPECT_TRUE(false, "wait returned early");
        }
    }
    END_TEST;
}


static bool TestFutexWaitBadAddress() {
    BEGIN_TEST;
    // Check that the wait address is checked for validity.
    zx_status_t rc = zx_futex_wait(nullptr, 123, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    ASSERT_EQ(rc, ZX_ERR_INVALID_ARGS, "Futex wait should have reurned invalid_arg");
    END_TEST;
}

// Poll until the kernel says that the given thread is blocked on a futex.
static bool wait_until_blocked_on_some_futex(zx_handle_t thread) {
    for (;;) {
        zx_info_thread_t info;
        ASSERT_EQ(zx_object_get_info(thread, ZX_INFO_THREAD, &info,
                                     sizeof(info), nullptr, nullptr), ZX_OK);
        if (info.state == ZX_THREAD_STATE_RUNNING) {
            zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
            continue;
        }
        ASSERT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_FUTEX);
        return true;
    }
}

// This starts a thread which waits on a futex.  We can do futex_wake()
// operations and then test whether or not this thread has been woken up.
class TestThread {
public:
    TestThread(volatile int32_t* futex_addr,
               zx_duration_t timeout_in_us = ZX_TIME_INFINITE)
        : futex_addr_(futex_addr),
          timeout_in_ns_(timeout_in_us) {
        auto ret = thrd_create_with_name(&thread_, wakeup_test_thread, this, "wakeup_test_thread");
        EXPECT_EQ(ret, thrd_success, "Error during thread creation");
        while (state_ == STATE_STARTED) {
            sched_yield();
        }
        // Note that this could fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");

        // We should only do this after state_ is STATE_ABOUT_TO_WAIT,
        // otherwise it could return when the thread has temporarily
        // blocked on a libc-internal futex.
        EXPECT_TRUE(wait_until_blocked_on_some_futex(get_thread_handle()));

        // This could also fail if futex_wait() gets a spurious wakeup.
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
    }

    TestThread(const TestThread &) = delete;
    TestThread& operator=(const TestThread &) = delete;

    ~TestThread() {
        if (handle_ != ZX_HANDLE_INVALID) {
            // kill_thread() was used, so the thrd_t is in undefined state.
            // Use the kernel handle to ensure the thread has died.
            EXPECT_EQ(zx_object_wait_one(handle_, ZX_THREAD_TERMINATED,
                                         ZX_TIME_INFINITE, NULL), ZX_OK,
                      "zx_object_wait_one failed on killed thread");
            EXPECT_EQ(zx_handle_close(handle_), ZX_OK,
                      "zx_handle_close failed on killed thread's handle");
            // The thrd_t and state associated with it is leaked at this point.
        } else {
            EXPECT_EQ(thrd_join(thread_, NULL), thrd_success,
                      "thrd_join failed");
        }
    }

    void assert_thread_woken() {
        while (state_ == STATE_ABOUT_TO_WAIT) {
            sched_yield();
        }
        EXPECT_EQ(state_, STATE_WAIT_RETURNED, "wrong state");
    }

    void assert_thread_not_woken() {
        EXPECT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
    }

    bool wait_for_timeout() {
        ASSERT_EQ(state_, STATE_ABOUT_TO_WAIT, "wrong state");
        while (state_ == STATE_ABOUT_TO_WAIT) {
            struct timespec wait_time = {0, 50 * 1000000 /* nanoseconds */};
            ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");
        }
        EXPECT_EQ(state_, STATE_WAIT_RETURNED, "wrong state");
        return true;
    }

    void kill_thread() {
        EXPECT_EQ(handle_, ZX_HANDLE_INVALID, "kill_thread called twice??");
        EXPECT_EQ(zx_handle_duplicate(thrd_get_zx_handle(thread_),
                                      ZX_RIGHT_SAME_RIGHTS, &handle_),
                  ZX_OK, "zx_handle_duplicate failed on thread handle");
        EXPECT_EQ(zx_task_kill(handle_), ZX_OK, "zx_task_kill() failed");
    }

    zx_handle_t get_thread_handle() {
        return thrd_get_zx_handle(thread_);
    }

private:
    static int wakeup_test_thread(void* thread_arg) {
        TestThread* thread = reinterpret_cast<TestThread*>(thread_arg);
        thread->state_ = STATE_ABOUT_TO_WAIT;
        zx_time_t deadline = thread->timeout_in_ns_ == ZX_TIME_INFINITE ? ZX_TIME_INFINITE :
                zx_deadline_after(thread->timeout_in_ns_);
        zx_status_t rc = zx_futex_wait(const_cast<int32_t*>(thread->futex_addr_),
                                       *thread->futex_addr_, ZX_HANDLE_INVALID, deadline);
        if (thread->timeout_in_ns_ == ZX_TIME_INFINITE) {
            EXPECT_EQ(rc, ZX_OK, "Error while wait");
        } else {
            EXPECT_EQ(rc, ZX_ERR_TIMED_OUT, "wait should have timedout");
        }
        thread->state_ = STATE_WAIT_RETURNED;
        return 0;
    }

    thrd_t thread_;
    volatile int32_t* futex_addr_;
    zx_duration_t timeout_in_ns_;
    zx_handle_t handle_ = ZX_HANDLE_INVALID;
    volatile enum {
        STATE_STARTED = 100,
        STATE_ABOUT_TO_WAIT = 200,
        STATE_WAIT_RETURNED = 300,
    } state_ = STATE_STARTED;
};

void check_futex_wake(volatile int32_t* futex_addr, int nwake) {
    zx_status_t rc = zx_futex_wake(const_cast<int32_t*>(futex_addr), nwake);
    EXPECT_EQ(rc, ZX_OK, "error during futex wait");
}

// Test that we can wake up a single thread.
bool TestFutexWakeup() {
    BEGIN_TEST;
    volatile int32_t futex_value = 1;
    TestThread thread(&futex_value);
    check_futex_wake(&futex_value, INT_MAX);
    thread.assert_thread_woken();
    END_TEST;
}

// Test that we can wake up multiple threads, and that futex_wake() heeds
// the wakeup limit.
bool TestFutexWakeupLimit() {
    BEGIN_TEST;
    volatile int32_t futex_value = 1;
    TestThread thread1(&futex_value);
    TestThread thread2(&futex_value);
    TestThread thread3(&futex_value);
    TestThread thread4(&futex_value);
    check_futex_wake(&futex_value, 2);
    // Test that threads are woken up in the order that they were added to
    // the wait queue.  This is not necessarily true for the Linux
    // implementation of futexes, but it is true for Zircon's
    // implementation.
    thread1.assert_thread_woken();
    thread2.assert_thread_woken();
    thread3.assert_thread_not_woken();
    thread4.assert_thread_not_woken();

    // Clean up: Wake the remaining threads so that they can exit.
    check_futex_wake(&futex_value, INT_MAX);
    thread3.assert_thread_woken();
    thread4.assert_thread_woken();
    END_TEST;
}

// Check that futex_wait() and futex_wake() heed their address arguments
// properly.  A futex_wait() call on one address should not be woken by a
// futex_wake() call on another address.
bool TestFutexWakeupAddress() {
    BEGIN_TEST;
    volatile int32_t futex_value1 = 1;
    volatile int32_t futex_value2 = 1;
    volatile int32_t dummy_addr = 1;
    TestThread thread1(&futex_value1);
    TestThread thread2(&futex_value2);

    check_futex_wake(&dummy_addr, INT_MAX);
    thread1.assert_thread_not_woken();
    thread2.assert_thread_not_woken();

    check_futex_wake(&futex_value1, INT_MAX);
    thread1.assert_thread_woken();
    thread2.assert_thread_not_woken();

    // Clean up: Wake the remaining thread so that it can exit.
    check_futex_wake(&futex_value2, INT_MAX);
    thread2.assert_thread_woken();
    END_TEST;
}

// Check that when futex_wait() times out, it removes the thread from
// the futex wait queue.
bool TestFutexUnqueuedOnTimeout() {
    BEGIN_TEST;
    volatile int32_t futex_value = 1;
    zx_status_t rc = zx_futex_wait(const_cast<int32_t*>(&futex_value),
                                   futex_value, ZX_HANDLE_INVALID, zx_deadline_after(1));
    ASSERT_EQ(rc, ZX_ERR_TIMED_OUT, "wait should have timedout");
    TestThread thread(&futex_value);
    // If the earlier futex_wait() did not remove itself from the wait
    // queue properly, the following futex_wake() call will attempt to wake
    // a thread that is no longer waiting, rather than waking the child
    // thread.
    check_futex_wake(&futex_value, 1);
    thread.assert_thread_woken();
    END_TEST;
}

// This tests for a specific bug in list handling.
bool TestFutexUnqueuedOnTimeout_2() {
    BEGIN_TEST;
    volatile int32_t futex_value = 10;
    TestThread thread1(&futex_value);
    TestThread thread2(&futex_value, ZX_MSEC(200));
    ASSERT_TRUE(thread2.wait_for_timeout());
    // With the bug present, thread2 was removed but the futex wait queue's
    // tail pointer still points to thread2.  When another thread is
    // enqueued, it gets added to the thread2 node and lost.

    TestThread thread3(&futex_value);
    check_futex_wake(&futex_value, 2);
    thread1.assert_thread_woken();
    thread3.assert_thread_woken();
    END_TEST;
}

// This tests for a specific bug in list handling.
bool TestFutexUnqueuedOnTimeout_3() {
    BEGIN_TEST;
    volatile int32_t futex_value = 10;
    TestThread thread1(&futex_value, ZX_MSEC(400));
    TestThread thread2(&futex_value);
    TestThread thread3(&futex_value);
    ASSERT_TRUE(thread1.wait_for_timeout());
    // With the bug present, thread1 was removed but the futex wait queue
    // is set to the thread2 node, which has an invalid (null) tail
    // pointer.  When another thread is enqueued, we get a null pointer
    // dereference or an assertion failure.

    TestThread thread4(&futex_value);
    check_futex_wake(&futex_value, 3);
    thread2.assert_thread_woken();
    thread3.assert_thread_woken();
    thread4.assert_thread_woken();
    END_TEST;
}

bool TestFutexRequeueValueMismatch() {
    BEGIN_TEST;
    int32_t futex_value1 = 100;
    int32_t futex_value2 = 200;
    zx_status_t rc = zx_futex_requeue(&futex_value1, 1, futex_value1 + 1,
                                            &futex_value2, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_ERR_BAD_STATE, "requeue should have returned bad state");
    END_TEST;
}

bool TestFutexRequeueSameAddr() {
    BEGIN_TEST;
    int32_t futex_value = 100;
    zx_status_t rc = zx_futex_requeue(&futex_value, 1, futex_value,
                                            &futex_value, 1, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_ERR_INVALID_ARGS, "requeue should have returned invalid args");
    END_TEST;
}

// Test that futex_requeue() can wake up some threads and requeue others.
bool TestFutexRequeue() {
    BEGIN_TEST;
    volatile int32_t futex_value1 = 100;
    volatile int32_t futex_value2 = 200;
    TestThread thread1(&futex_value1);
    TestThread thread2(&futex_value1);
    TestThread thread3(&futex_value1);
    TestThread thread4(&futex_value1);
    TestThread thread5(&futex_value1);
    TestThread thread6(&futex_value1);

    zx_status_t rc = zx_futex_requeue(
        const_cast<int32_t*>(&futex_value1), 3, futex_value1,
        const_cast<int32_t*>(&futex_value2), 2, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_OK, "Error in requeue");
    // 3 of the threads should have been woken.
    thread1.assert_thread_woken();
    thread2.assert_thread_woken();
    thread3.assert_thread_woken();
    thread4.assert_thread_not_woken();
    thread5.assert_thread_not_woken();
    thread6.assert_thread_not_woken();

    // Since 2 of the threads should have been requeued, waking all the
    // threads on futex_value2 should wake 2 threads.
    check_futex_wake(&futex_value2, INT_MAX);
    thread4.assert_thread_woken();
    thread5.assert_thread_woken();
    thread6.assert_thread_not_woken();

    // Clean up: Wake the remaining thread so that it can exit.
    check_futex_wake(&futex_value1, 1);
    thread6.assert_thread_woken();
    END_TEST;
}

// Test the case where futex_wait() times out after having been moved to a
// different queue by futex_requeue().  Check that futex_wait() removes
// itself from the correct queue in that case.
bool TestFutexRequeueUnqueuedOnTimeout() {
    BEGIN_TEST;
    zx_duration_t timeout_in_ns = ZX_MSEC(300);
    volatile int32_t futex_value1 = 100;
    volatile int32_t futex_value2 = 200;
    TestThread thread1(&futex_value1, timeout_in_ns);
    zx_status_t rc = zx_futex_requeue(
        const_cast<int32_t*>(&futex_value1), 0, futex_value1,
        const_cast<int32_t*>(&futex_value2), INT_MAX, ZX_HANDLE_INVALID);
    ASSERT_EQ(rc, ZX_OK, "Error in requeue");
    TestThread thread2(&futex_value2);
    // thread1 and thread2 should now both be waiting on futex_value2.

    ASSERT_TRUE(thread1.wait_for_timeout());
    thread2.assert_thread_not_woken();
    // thread1 should have removed itself from futex_value2's wait queue,
    // so only thread2 should be waiting on futex_value2.  We can test that
    // by doing futex_wake() with count=1.

    check_futex_wake(&futex_value2, 1);
    thread2.assert_thread_woken();
    END_TEST;
}

// Test that we can successfully kill a thread that is waiting on a futex,
// and that we can join the thread afterwards.  This checks that waiting on
// a futex does not leave the thread in an unkillable state.
bool TestFutexThreadKilled() {
    BEGIN_TEST;
    volatile int32_t futex_value = 1;
    // Note: TestThread will ensure the kernel thread died, though
    // it's not possible to thrd_join after killing the thread.
    TestThread thread(&futex_value);
    thread.kill_thread();

    // Check that the futex_wait() syscall does not return control to
    // userland before the thread gets killed.
    struct timespec wait_time = {0, 10 * 1000000 /* nanoseconds */};
    ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");
    thread.assert_thread_not_woken();

    END_TEST;
}

// Test that the futex_wait() syscall is restarted properly if the thread
// calling it gets suspended and resumed.  (This tests for a bug where the
// futex_wait() syscall would return ZX_ERR_TIMED_OUT and not get restarted by
// the syscall wrapper in the VDSO.)
static bool TestFutexThreadSuspended() {
    BEGIN_TEST;
    volatile int32_t futex_value = 1;
    TestThread thread(&futex_value);

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread.get_thread_handle(),
                                    &suspend_token), ZX_OK);
    // Wait some time for the thread suspension to take effect.
    struct timespec wait_time = {0, 10 * 1000000 /* nanoseconds */};
    ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    // Wait some time for the thread to resume and execute.
    ASSERT_EQ(nanosleep(&wait_time, NULL), 0, "Error during sleep");

    thread.assert_thread_not_woken();
    check_futex_wake(&futex_value, 1);
    thread.assert_thread_woken();

    END_TEST;
}

// Test that misaligned pointers cause futex syscalls to return a failure.
static bool TestFutexMisaligned() {
    BEGIN_TEST;

    // Make sure the whole thing is aligned, so the 'futex' member will
    // definitely be misaligned.
    alignas(zx_futex_t) struct {
        uint8_t misalign;
        zx_futex_t futex[2];
    } __attribute__((packed)) buffer;
    zx_futex_t* const futex = &buffer.futex[0];
    zx_futex_t* const futex_2 = &buffer.futex[1];
    ASSERT_GT(alignof(zx_futex_t), 1);
    ASSERT_NE((uintptr_t)futex % alignof(zx_futex_t), 0);
    ASSERT_NE((uintptr_t)futex_2 % alignof(zx_futex_t), 0);

    // zx_futex_requeue might check the waited-for value before it
    // checks the second futex's alignment, so make sure the call is
    // valid other than the alignment.  (Also don't ask anybody to
    // look at uninitialized stack space!)
    memset(&buffer, 0, sizeof(buffer));

    ASSERT_EQ(zx_futex_wait(futex, 0, ZX_HANDLE_INVALID, ZX_TIME_INFINITE), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_futex_wake(futex, 1), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_futex_requeue(futex, 1, 0, futex_2, 1, ZX_HANDLE_INVALID), ZX_ERR_INVALID_ARGS);

    END_TEST;
}

static void log(const char* str) {
    zx_time_t now = zx_clock_get_monotonic();
    unittest_printf("[%08" PRIu64 ".%08" PRIu64 "]: %s",
                    now / 1000000000, now % 1000000000, str);
}

class Event {
public:
    Event()
        : signaled_(0) {}

    void wait() {
        if (signaled_ == 0) {
            zx_futex_wait(&signaled_, signaled_, ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
        }
    }

    void signal() {
        if (signaled_ == 0) {
            signaled_ = 1;
            zx_futex_wake(&signaled_, UINT32_MAX);
        }
    }

private:
    int32_t signaled_;
};

static Event event;

static int signal_thread1(void* arg) {
    log("thread 1 waiting on event\n");
    event.wait();
    log("thread 1 done\n");
    return 0;
}

static int signal_thread2(void* arg) {
    log("thread 2 waiting on event\n");
    event.wait();
    log("thread 2 done\n");
    return 0;
}

static int signal_thread3(void* arg) {
    log("thread 3 waiting on event\n");
    event.wait();
    log("thread 3 done\n");
    return 0;
}

static bool TestEventSignaling() {
    BEGIN_TEST;
    thrd_t thread1, thread2, thread3;

    log("starting signal threads\n");
    thrd_create_with_name(&thread1, signal_thread1, NULL, "thread 1");
    thrd_create_with_name(&thread2, signal_thread2, NULL, "thread 2");
    thrd_create_with_name(&thread3, signal_thread3, NULL, "thread 3");

    zx_nanosleep(zx_deadline_after(ZX_MSEC(300)));
    log("signaling event\n");
    event.signal();

    log("joining signal threads\n");
    thrd_join(thread1, NULL);
    log("signal_thread 1 joined\n");
    thrd_join(thread2, NULL);
    log("signal_thread 2 joined\n");
    thrd_join(thread3, NULL);
    log("signal_thread 3 joined\n");
    END_TEST;
}

BEGIN_TEST_CASE(futex_tests)
RUN_TEST(TestFutexWaitValueMismatch);
RUN_TEST(TestFutexWaitTimeout);
RUN_TEST(TestFutexWaitTimeoutElapsed);
RUN_TEST(TestFutexWaitBadAddress);
RUN_TEST(TestFutexWakeup);
RUN_TEST(TestFutexWakeupLimit);
RUN_TEST(TestFutexWakeupAddress);
RUN_TEST(TestFutexUnqueuedOnTimeout);
RUN_TEST(TestFutexUnqueuedOnTimeout_2);
RUN_TEST(TestFutexUnqueuedOnTimeout_3);
RUN_TEST(TestFutexRequeueValueMismatch);
RUN_TEST(TestFutexRequeueSameAddr);
RUN_TEST(TestFutexRequeue);
RUN_TEST(TestFutexRequeueUnqueuedOnTimeout);
RUN_TEST(TestFutexThreadKilled);
RUN_TEST(TestFutexThreadSuspended);
RUN_TEST(TestFutexMisaligned);
RUN_TEST(TestEventSignaling);
END_TEST_CASE(futex_tests)
