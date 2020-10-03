#define OS_AVAILABLE(_target, _availability)                                   \
  __attribute__((availability(_target, _availability)))

extern int iOSAPI() OS_AVAILABLE(ios, introduced = 12.0)
    OS_AVAILABLE(macos, unavailable);
