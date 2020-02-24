//
//  Tweaks.cpp
//  ld
//
//  Created by Michael Eisel on 2/19/20.
//  Copyright © 2020 Apple Inc. All rights reserved.
//

#include "Tweaks.hpp"

namespace Tweaks {
    bool reproEnabled() {
#ifdef REPRO
		return true;
#else
		return false;
#endif
    }
}
