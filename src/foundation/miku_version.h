#ifndef MIKU_VERSION_H
#define MIKU_VERSION_H

#define MIKU_VERSION_MAJOR 0
#define MIKU_VERSION_MINOR 1
#define MIKU_VERSION_PATCH 0

#define MIKU_VERSION_STRING "0.1.0"

#ifndef MIKU_BUILD_DATE
#define MIKU_BUILD_DATE __DATE__ " " __TIME__
#endif

#ifndef MIKU_GIT_HASH
#define MIKU_GIT_HASH "unknown"
#endif

#define MIKU_VERSION_FULL "miku " MIKU_VERSION_STRING " (" MIKU_GIT_HASH ", " MIKU_BUILD_DATE ")"

#endif
