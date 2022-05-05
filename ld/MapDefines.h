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

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"


#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"

#ifdef REPRO

#define LDOrderedMap std::map
#define LDMap std::unordered_map
#define LDFastMap std::unordered_map
#define LDSet std::unordered_set
#define LDFastSet std::unordered_set
#define LDOrderedSet std::set

#else

#define LDOrderedMap absl::btree_map
#define LDMap absl::node_hash_map
#define LDFastMap absl::flat_hash_map
#define LDSet absl::node_hash_set
#define LDOrderedSet absl::btree_set

#endif


#endif /* MapDefines_h */
