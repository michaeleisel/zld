/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */


#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <dlfcn.h>
#include <libkern/OSByteOrder.h>

#include <vector>
#include <map>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"
#include "ld.hpp"
#include "thread_starts.h"

namespace ld {
namespace passes {
namespace thread_starts {


static std::map<const Atom*, uint64_t> sAtomToAddress;




template <typename A>
class ThreadStartsAtom : public ld::Atom {
public:
											ThreadStartsAtom(uint32_t fixupAlignment, uint32_t numThreadStarts);
											~ThreadStartsAtom();

	virtual const ld::File*					file() const					{ return NULL; }
	virtual const char*						name() const					{ return "thread starts"; }
	virtual uint64_t						size() const					{ return 4 + (_numThreadStarts * 4); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		uint32_t header = 0;
		if (_fixupAlignment == 8)
			header |= 1;
		bzero(buffer, size());
		A::P::E::set32(*((uint32_t*)(&buffer[0])), header); // header
		// Fill in offsets with 0xFFFFFFFF's for now as that wouldn't be a valid offset
		memset(&buffer[4], 0xFFFFFFFF, _numThreadStarts * sizeof(uint32_t));
	}
	virtual void							setScope(Scope)					{ }
	virtual Fixup::iterator					fixupsBegin() const	{ return NULL; }
	virtual Fixup::iterator					fixupsEnd() const	{ return NULL; }

private:

	uint32_t								_fixupAlignment;
	uint32_t								_numThreadStarts;

	static bool								_s_log;
	static ld::Section						_s_section;
};

template <typename A>
bool ThreadStartsAtom<A>::_s_log = false;

template <typename A>
ld::Section ThreadStartsAtom<A>::_s_section("__TEXT", "__thread_starts", ld::Section::typeThreadStarts);


template <typename A>
ThreadStartsAtom<A>::ThreadStartsAtom(uint32_t fixupAlignment, uint32_t numThreadStarts)
	: ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
				ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified,
				symbolTableNotIn, false, false, false, ld::Atom::Alignment(2)),
		_fixupAlignment(fixupAlignment), _numThreadStarts(numThreadStarts)
{
	assert(_fixupAlignment == 4 || _fixupAlignment == 8);
}

template <typename A>
ThreadStartsAtom<A>::~ThreadStartsAtom()
{
}


static void buildAddressMap(const Options& opts, ld::Internal& state) {
	// Assign addresses to sections
	state.setSectionSizesAndAlignments();
	state.assignFileOffsets();

	// Assign addresses to atoms in a side table
	const bool log = false;
	if ( log ) fprintf(stderr, "buildAddressMap()\n");
	for (std::vector<ld::Internal::FinalSection*>::iterator sit = state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		uint16_t maxAlignment = 0;
		uint64_t offset = 0;
		if ( log ) fprintf(stderr, "  section=%s/%s, address=0x%08llX\n", sect->segmentName(), sect->sectionName(), sect->address);
		for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
			const ld::Atom* atom = *ait;
			uint32_t atomAlignmentPowerOf2 = atom->alignment().powerOf2;
			uint32_t atomModulus = atom->alignment().modulus;
			if ( atomAlignmentPowerOf2 > maxAlignment )
				maxAlignment = atomAlignmentPowerOf2;
			// calculate section offset for this atom
			uint64_t alignment = 1 << atomAlignmentPowerOf2;
			uint64_t currentModulus = (offset % alignment);
			uint64_t requiredModulus = atomModulus;
			if ( currentModulus != requiredModulus ) {
				if ( requiredModulus > currentModulus )
					offset += requiredModulus-currentModulus;
				else
					offset += requiredModulus+alignment-currentModulus;
			}

			if ( log ) fprintf(stderr, "    0x%08llX atom=%p, name=%s\n", sect->address+offset, atom, atom->name());
			sAtomToAddress[atom] = sect->address + offset;

			offset += atom->size();
		}
	}
}

static uint32_t threadStartsCountInSection(std::vector<uint64_t>& fixupAddressesInSection) {
	if (fixupAddressesInSection.empty())
		return 0;

	std::sort(fixupAddressesInSection.begin(), fixupAddressesInSection.end());

	uint32_t numThreadStarts = 0;

	// Walk all the fixups, and compute the number of rebase chains we need, assuming
	// 11-bits of delta, with 4-byte alignment for each entry.
	const uint64_t deltaBits = 11;
	const uint64_t minAlignment = 4;

	uint64_t prevAddress = 0;
	for (uint64_t address : fixupAddressesInSection) {
		uint64_t delta = address - prevAddress;
		assert( (delta & (minAlignment - 1)) == 0 );
		delta /= minAlignment;
		if (delta >= (1 << deltaBits)) {
			++numThreadStarts;
		}
		prevAddress = address;
	}
	fixupAddressesInSection.clear();

	return numThreadStarts;
}

static uint32_t processSections(ld::Internal& state, uint64_t minAlignment) {
	uint32_t numThreadStarts = 0;

	std::vector<uint64_t> fixupAddressesInSection;
	for (ld::Internal::FinalSection* sect : state.sections) {
		if ( sect->isSectionHidden() )
			continue;
		for (const ld::Atom* atom : sect->atoms) {
			bool seenTarget = false;
			for (ld::Fixup::iterator fit = atom->fixupsBegin(), end=atom->fixupsEnd(); fit != end; ++fit) {
				if ( fit->firstInCluster() ) {
					seenTarget = false;
				}
				if ( fit->setsTarget(false) ) {
					seenTarget = true;
				}
				if ( !fit->lastInCluster() )
					continue;
				if ( !fit->isStore() )
					continue;
				if ( fit->isPcRelStore(false) )
					continue;
				if ( !seenTarget )
					continue;
				uint64_t address = sAtomToAddress[atom] + fit->offsetInAtom;
				fixupAddressesInSection.push_back(address);

				if ( (address & (minAlignment-1)) != 0 ) {
					throwf("pointer not aligned at address 0x%llX (%s + %d from %s)",
						   address, atom->name(), fit->offsetInAtom, atom->safeFilePath());
				}
			}
		}
		numThreadStarts += threadStartsCountInSection(fixupAddressesInSection);
	}

	return numThreadStarts;
}

void doPass(const Options& opts, ld::Internal& state)
{
	if ( !opts.makeThreadedStartsSection() )
		return;

	buildAddressMap(opts, state);

	const uint32_t fixupAlignment = 4;
	uint32_t numThreadStarts = processSections(state, fixupAlignment);

	// create atom that contains the whole compact unwind table
	switch ( opts.architecture() ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			state.addAtom(*new ThreadStartsAtom<x86_64>(fixupAlignment, numThreadStarts));
			break;
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			state.addAtom(*new ThreadStartsAtom<x86>(fixupAlignment, numThreadStarts));
			break;
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
			state.addAtom(*new ThreadStartsAtom<arm64>(fixupAlignment, numThreadStarts));
			break;
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			state.addAtom(*new ThreadStartsAtom<arm>(fixupAlignment, numThreadStarts));
			break;
#endif
		default:
			assert(0 && "no threaded starts for arch");
	}
}


} // namespace thread_starts
} // namespace passes 
} // namespace ld 
