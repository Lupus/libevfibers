/********************************************************************

  Copyright 2012 Konstantin Olkhovskiy <lupus@oxnull.net>

  This file is part of libevfibers.

  libevfibers is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or any later version.

  libevfibers is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with libevfibers.  If not, see
  <http://www.gnu.org/licenses/>.

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
