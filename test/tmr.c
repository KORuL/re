/**
 * @file tmr.c  Timers testcode
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include "test.h"


#define DEBUG_MODULE "testtmr"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static void tmr_handler(void *arg)
{
	(void)arg;
}


struct tmr_thread_test {
	struct tmr tmr;
	RE_ATOMIC uint32_t ready;
	RE_ATOMIC uint32_t done;
	RE_ATOMIC bool start;
	RE_ATOMIC bool close;
	RE_ATOMIC int worker_err;
};


static int tmr_thread(void *arg)
{
	struct tmr_thread_test *test = arg;
	int err;
	unsigned i;

	err = re_thread_init();
	if (err) {
		re_atomic_rls_set(&test->worker_err, err);
		re_atomic_rlx_add(&test->ready, 1u);
		return err;
	}

	re_atomic_rlx_add(&test->ready, 1u);
	while (!re_atomic_acq(&test->start))
		sys_usleep(100);

	for (i = 0; i < 10000; ++i) {
		tmr_start(&test->tmr, 60000, tmr_handler, NULL);
		if (i & 1)
			tmr_cancel(&test->tmr);
		sys_usleep(1);
	}

	re_atomic_rlx_add(&test->done, 1u);
	while (!re_atomic_acq(&test->close))
		sys_usleep(100);

	re_thread_close();
	return 0;
}


int test_tmr_thread_ownership(void)
{
	struct tmr_thread_test test;
	thrd_t threads[2];
	uint64_t deadline;
	unsigned nthreads = 0;
	unsigned i;
	int thread_err;
	int err = 0;

	memset(&test, 0, sizeof(test));
	tmr_init(&test.tmr);

	/* A cancelled timer must not retain a stale running state. */
	tmr_start(&test.tmr, 60000, tmr_handler, NULL);
	tmr_cancel(&test.tmr);
	TEST_ASSERT(!tmr_isrunning(&test.tmr));
	TEST_EQUALS(NULL, test.tmr.le.list);
	TEST_EQUALS((uintptr_t)NULL, re_atomic_acq(&test.tmr.llock));

	for (i = 0; i < RE_ARRAY_SIZE(threads); ++i) {
		err = thread_create_name(&threads[i], "tmr-ownership",
					 tmr_thread, &test);
		TEST_ERR(err);
		++nthreads;
	}

	deadline = tmr_jiffies() + 5000;
	while (re_atomic_acq(&test.ready) < RE_ARRAY_SIZE(threads)) {
		if (tmr_jiffies() >= deadline) {
			err = ETIMEDOUT;
			goto out;
		}
		sys_usleep(100);
	}

	err = re_atomic_acq(&test.worker_err);
	TEST_ERR(err);

	re_atomic_rls_set(&test.start, true);
	deadline = tmr_jiffies() + 15000;
	while (re_atomic_acq(&test.done) < RE_ARRAY_SIZE(threads)) {
		if (tmr_jiffies() >= deadline) {
			err = ETIMEDOUT;
			goto out;
		}
		sys_usleep(100);
	}

out:
	re_atomic_rls_set(&test.start, true);
	re_atomic_rls_set(&test.close, true);

	for (i = 0; i < nthreads; ++i) {
		thread_err = 0;
		(void)thrd_join(threads[i], &thread_err);
		if (!err && thread_err)
			err = thread_err;
	}

	if (!err && (tmr_isrunning(&test.tmr) || test.tmr.le.list ||
		    re_atomic_acq(&test.tmr.llock)))
		err = EPROTO;

	tmr_cancel(&test.tmr);
	return err;
}


int test_tmr_jiffies(void)
{
	uint64_t tmr_start, tmr_end, diff;
	int err = 0;

	tmr_start = tmr_jiffies();
	sys_msleep(1);
	tmr_end = tmr_jiffies();
	diff = tmr_end - tmr_start;

	TEST_ASSERT(diff >= 1);
	TEST_ASSERT(diff < 50);

out:
	return err;
}


int test_tmr_jiffies_usec(void)
{
	uint64_t tmr_start, diff;
	int i;
	int err = 0;

	tmr_start = tmr_jiffies_usec();
	diff = 0;
	for (i = 0; i < 100000 && !diff; i++)
		diff = tmr_jiffies_usec() - tmr_start;

	TEST_ASSERT(diff >= 1);
	TEST_ASSERT(diff < 1000);

out:
	return err;
}
