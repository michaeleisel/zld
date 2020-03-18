//
//  MapDefines.h
//  ld64
//
//  Created by Michael Eisel on 2/19/20.
//  Copyright © 2020 Apple Inc. All rights reserved.
//

#ifndef MapDefines_h
#define MapDefines_h

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"

#ifdef REPRO

#define std::map std::map
#define std::unordered_map std::unordered_map
#define std::unordered_set std::unordered_set
#define std::set std::set

#else

#define std::map absl::btree_map
#define std::unordered_map absl::flat_hash_map
#define std::unordered_set absl::flat_hash_set
#define std::set absl::btree_set

#endif


#endif /* MapDefines_h */
