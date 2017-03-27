#!/bin/bash
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at:
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

RED='\033[0;31m'
GREEN='\033[0;32m'
RESET='\033[0m'

if ! which grep &> /dev/null; then
	exit 1
fi
EGREP="grep -E"

# Valgrind error pattern, see:
#     http://valgrind.org/docs/manual/mc-manual.html#mc-manual.errormsgs

valgrind_def_leak='definitely lost in'
valgrind_invalid_rw='Invalid (write|read) of size'
valgrind_invalid_free='(Invalid|Mismatched) free'
valgrind_uninit_jmp='Conditional jump or move depends on uninitialised value'
valgrind_uninit_syscall='Syscall param write(buf) points to uninitialised'
valgrind_overlap='Source and destination overlap in'
valgrind_output=$1

echo -n 'Check definitely memory leak... '
if $EGREP -r "$valgrind_def_leak" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo -n 'Check invalid read/write... '
if $EGREP -r "$valgrind_invalid_rw" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo -n 'Check invalid free... '
if $EGREP -r "$valgrind_invalid_free" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo -n 'Check use of uninitialised values... '
if $EGREP -r "$valgrind_uninit_jmp" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo -n 'Check use of uninitialised or unaddressable values in system calls... '
if $EGREP -r "$valgrind_uninit_syscall" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo -n 'Check overlapping source and destination blocks... '
if $EGREP -r "$valgrind_overlap" $valgrind_output > /dev/null; \
	then echo -e "${RED}FAILED${RESET}"; \
	else echo -e "${GREEN}ok${RESET}";   \
fi
echo '---------------------------------------------------------------------------------'
exit 0
