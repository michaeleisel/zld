#import "ld.hpp"
#import <mach/mach_time.h>

static double currentTime() {
  uint64_t time = mach_absolute_time();
  static uint64_t sUnitsPerSecond = 0;
  if ( sUnitsPerSecond == 0 ) {
    struct mach_timebase_info timeBaseInfo;
    if ( mach_timebase_info(&timeBaseInfo) != KERN_SUCCESS )
      abort();
    sUnitsPerSecond = 1000000000ULL * timeBaseInfo.denom / timeBaseInfo.numer;
  }
  return (double)time / (double)sUnitsPerSecond;
}

static void printTimeQuick(uint64_t time) {
    printTime("", time, time);
}

template <typename Func>
static void timeBlock(const char *name, Func f) {
    uint64_t start = mach_absolute_time();
    f();
    uint64_t end = mach_absolute_time();
    printTime(name, end - start, end - start);
}
