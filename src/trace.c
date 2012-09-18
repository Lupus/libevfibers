/********************************************************************

  Copyright 2012 Konstantin Olkhovskiy <lupus@oxnull.net>

  This file is part of Mersenne.

  Mersenne is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  any later version.

  Mersenne is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Mersenne.  If not, see <http://www.gnu.org/licenses/>.

 ********************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>
#include <evfibers_private/trace.h>

void fill_trace_info(struct trace_info *info)
{
	info->size = backtrace(info->array, TRACE_SIZE);
}

void print_trace_info(struct trace_info *info)
{
	size_t i;
	char **strings = backtrace_symbols(info->array, info->size);
	for (i = 0; i < info->size; i++)
		fprintf(stderr, "%s\n", strings[i]);
	free(strings);
}
