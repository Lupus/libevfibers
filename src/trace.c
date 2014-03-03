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
#include <stdio.h>
#include <execinfo.h>
#include <evfibers_private/trace.h>
#include <evfibers_private/fiber.h>

void fill_trace_info(FBR_P_ struct trace_info *info)
{
	if (0 == fctx->__p->backtraces_enabled)
		return;
	info->size = backtrace(info->array, TRACE_SIZE);
}

void print_trace_info(FBR_P_ struct trace_info *info, fbr_logutil_func_t log)
{
	size_t i;
	char **strings;
	if (0 == fctx->__p->backtraces_enabled) {
		(*log)(FBR_A_ "(No backtrace since they are disabled)");
		return;
	}
	strings = backtrace_symbols(info->array, info->size);
	for (i = 0; i < info->size; i++)
		(*log)(FBR_A_ "%s", strings[i]);
	free(strings);
}
