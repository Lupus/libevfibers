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

#ifndef _FBR_TRACE_PRIVATE_H_
#define _FBR_TRACE_PRIVATE_H_

#include <evfibers/fiber.h>

#define TRACE_SIZE 16

struct trace_info {
       void *array[TRACE_SIZE];
       size_t size;
};

void fill_trace_info(FBR_P_ struct trace_info *info);
void print_trace_info(FBR_P_ struct trace_info *info, fbr_logutil_func_t log);

#endif
