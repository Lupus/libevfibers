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

#include <stdlib.h>
#include <check.h>
#include <assert.h>

#include <evfibers/config.h>
#include "init.h"
#include "mutex.h"
#include "cond.h"
#include "reclaim.h"
#include "io.h"
#include "logger.h"
#include "buffer.h"
#include "key.h"
#include "eio.h"

Suite *evfibers_suite(void)
{
	Suite *s;
	TCase *tc_init, *tc_mutex, *tc_cond, *tc_reclaim, *tc_io, *tc_logger,
	      *tc_buffer, *tc_key, *tc_eio;

	s = suite_create ("evfibers");
	tc_init = init_tcase();
	tc_mutex = mutex_tcase();
	tc_cond = cond_tcase();
	tc_reclaim = reclaim_tcase();
	tc_io = io_tcase();
	tc_logger = logger_tcase();
	tc_buffer = buffer_tcase();
	tc_key = key_tcase();
	tc_eio = eio_tcase();
	suite_add_tcase(s, tc_init);
	suite_add_tcase(s, tc_mutex);
	suite_add_tcase(s, tc_cond);
	suite_add_tcase(s, tc_reclaim);
	suite_add_tcase(s, tc_io);
	suite_add_tcase(s, tc_logger);
	suite_add_tcase(s, tc_buffer);
	suite_add_tcase(s, tc_key);
	suite_add_tcase(s, tc_eio);

	return s;
}

int main(void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	srand(42);

	/* pbuilder workaround: it does not mount tmpfs to /dev/shm */
	setenv("FBR_BUFFER_FILE_PATTERN", "/tmp/pbuilder_fbr_buffer.XXXXXX", 0);

	s = evfibers_suite();
	sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
