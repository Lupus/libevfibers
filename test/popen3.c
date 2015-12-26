/********************************************************************

   Copyright 2013 Konstantin Olkhovskiy <lupus@oxnull.net>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

 ********************************************************************/

#include <check.h>
#include <evfibers/config.h>

#include <errno.h>
#include <ev.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <evfibers_private/fiber.h>

static void assert_pipe_content(int fd, const char *content)
{
	ssize_t retval;
	const size_t len = strlen(content);
	char *buf;
	buf = alloca(len + 1);
	retval = read(fd, buf, len);
	fail_if(retval < 0);
	fail_unless((size_t)retval == len);
	retval = read(fd, buf, len);
	fail_unless(retval == 0);
	fail_if(memcmp(content, buf, len));
}

static void assert_pipe_empty(int fd)
{
	ssize_t retval;
	char buf[16];
	retval = read(fd, buf, sizeof(buf));
	fail_unless(retval == 0);
}

static void popen_fiber(FBR_P_ void *_arg)
{
	pid_t pid;
	int retval;
	int stdin_w, stdout_r, stderr_r;
	//char buf[512];
	(void)_arg;
	const char *echo_test_str = "this is mirror test message";

	/* Simple stdout test */
	pid = fbr_popen3(
		FBR_A_ "/bin/bash",
		(char *[]){
			"/bin/bash", "-c", "echo 'simple stdout test'", NULL,
		},
		(char *[]){NULL}, "/", &stdin_w, &stdout_r, &stderr_r);
	fail_unless(pid > 0);
	retval = fbr_waitpid(FBR_A_ pid);
	fail_unless(retval == 0);
	assert_pipe_content(stdout_r, "simple stdout test\n");
	assert_pipe_empty(stderr_r);
	fail_if(close(stdin_w) != 0);
	fail_if(close(stdout_r) != 0);
	fail_if(close(stderr_r) != 0);

	/* Same as above, but no input and error requested */
	pid = fbr_popen3(
		FBR_A_ "/bin/bash",
		(char *[]){
			"/bin/bash", "-c", "echo 'simple stdout test2'", NULL,
		},
		(char *[]){NULL}, "/", NULL, &stdout_r, NULL);
	fail_unless(pid > 0);
	retval = fbr_waitpid(FBR_A_ pid);
	fail_unless(retval == 0);
	assert_pipe_content(stdout_r, "simple stdout test2\n");
	fail_if(close(stdout_r) != 0);

	/* Just run something */
	pid = fbr_popen3(
		FBR_A_ "/bin/ls",
		(char *[]){
			"/bin/ls", NULL,
		},
		(char *[]){NULL}, NULL, NULL, NULL, NULL);
	fail_unless(pid > 0);
	retval = fbr_waitpid(FBR_A_ pid);
	fail_unless(retval == 0);

	/* Simple stderr test */
	pid = fbr_popen3(FBR_A_ "/bin/bash",
			 (char *[]){
				 "/bin/bash", "-c",
				 "echo 'simple stderr test' >&2", NULL,
			 },
			 (char *[]){NULL}, "/", &stdin_w, &stdout_r, &stderr_r);
	fail_unless(pid > 0);
	retval = fbr_waitpid(FBR_A_ pid);
	fail_unless(retval == 0);
	assert_pipe_empty(stdout_r);
	assert_pipe_content(stderr_r, "simple stderr test\n");
	fail_if(close(stdin_w) != 0);
	fail_if(close(stdout_r) != 0);
	fail_if(close(stderr_r) != 0);

	/* Echo application test */
	pid = fbr_popen3(FBR_A_ "/bin/cat",
			 (char *[]){
				 "/bin/cat", NULL,
			 },
			 (char *[]){NULL}, "/", &stdin_w, &stdout_r, &stderr_r);
	fail_unless(pid > 0);
	retval = write(stdin_w, echo_test_str, strlen(echo_test_str));
	fail_unless(retval == (int)strlen(echo_test_str));
	fail_if(close(stdin_w) != 0);

	retval = fbr_waitpid(FBR_A_ pid);
	fail_unless(retval == 0);
	assert_pipe_empty(stderr_r);
	assert_pipe_content(stdout_r, echo_test_str);
	fail_if(close(stdout_r) != 0);
	fail_if(close(stderr_r) != 0);
}


START_TEST(test_popen3)
{
	int retval;
	fbr_id_t fiber = FBR_ID_NULL;
	struct fbr_context context;
	fbr_init(&context, EV_DEFAULT);
	signal(SIGPIPE, SIG_IGN);

	fiber = fbr_create(&context, "popen_fiber", popen_fiber, NULL, 0);
	fail_if(fbr_id_isnull(fiber));
	retval = fbr_transfer(&context, fiber);
	fail_unless(0 == retval, NULL);

	ev_run(EV_DEFAULT, 0);
	fbr_destroy(&context);
}
END_TEST

TCase *popen3_tcase(void)
{
	TCase *tc_popen3 = tcase_create("popen3");
	tcase_add_test(tc_popen3, test_popen3);
	return tc_popen3;
}
