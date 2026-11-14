# Match erts/configure.ac:3360-3425 at UPSTREAM_COMMIT. Having poll() is
# insufficient: a fallback pollset must also handle devices such as /dev/null.
# Keep the independently selected epoll/kqueue primary pollset unchanged.
include(CheckCSourceRuns)
include(CMakePushCheckState)

if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Working poll() detection requires the supported native build")
endif()

set(ERTS_USE_POLL 0)
set(USE_SELECT 1)
if(HAVE_POLL AND HAVE_POLL_H)
    cmake_push_check_state(RESET)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_REQUIRED_DEFINITIONS -D_DARWIN_C_SOURCE=)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
    endif()
    # This is the pinned AC_RUN_IFELSE body, including its select-emulation
    # guard and device/POLLNVAL check. Do not replace it with a symbol check.
    check_c_source_runs([=[
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
int main()
{
#ifdef _POLL_EMUL_H_
  exit(1); /* Implemented using select() -- fail */
#else
  struct pollfd fds[1];
  int fd;
  fd = open("/dev/null", 1);
  fds[0].fd = fd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  if (poll(fds, 1, 0) < 0 || (fds[0].revents & POLLNVAL) != 0) {
    exit(1);  /* Does not work for devices -- fail */
  }
  exit(0);
#endif
}
]=] ERTS_HAVE_WORKING_POLL)
    cmake_pop_check_state()
    if(ERTS_HAVE_WORKING_POLL)
        set(ERTS_USE_POLL 1)
        set(USE_SELECT 0)
    endif()
endif()
# config.h uses #cmakedefine: false leaves ERTS_USE_POLL undefined. Its
# presence, not a numeric value, selects poll in the retained erl_poll.h.
