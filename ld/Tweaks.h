//
//  Tweaks.hpp
//  ld
//
//  Created by Michael Eisel on 2/19/20.
//  Copyright © 2020 Apple Inc. All rights reserved.
//

#ifndef Tweaks_hpp
#define Tweaks_hpp

#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((always_inline)) static bool tweaksReproEnabled(void);

#ifdef __cplusplus
}
#endif

__attribute__((always_inline)) static bool tweaksReproEnabled() {
#ifdef REPRO
    return true;
#else
    return false;
#endif
}

#endif /* Tweaks_hpp */
