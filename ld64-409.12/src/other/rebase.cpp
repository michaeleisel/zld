/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
 *
 * Copyright (c) 2006-2012 Apple Inc. All rights reserved.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <set>


#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"

static bool verbose = false;

__attribute__((noreturn))
void throwf(const char* format, ...) 
{
	va_list	list;
	char*	p;
	va_start(list, format);
	vasprintf(&p, format, list);
	va_end(list);
	
	const char*	t = p;
	throw t;
}


class AbstractRebaser
{
public:
	virtual cpu_type_t							getArchitecture() const = 0;
	virtual uint64_t							getBaseAddress() const = 0;
	virtual uint64_t							getVMSize() const = 0;
	virtual void								setBaseAddress(uint64_t) = 0;
};


template <typename A>
class Rebaser : public AbstractRebaser
{
public:
												Rebaser(const void* machHeader);
	virtual										~Rebaser() {}

	virtual cpu_type_t							getArchitecture() const;
	virtual uint64_t							getBaseAddress() const;
	virtual uint64_t							getVMSize() const;
	virtual void								setBaseAddress(uint64_t);

private:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;
	
	struct vmmap { pint_t vmaddr; pint_t vmsize; pint_t fileoff; };
	
	void										setRelocBase();
	void										buildSectionTable();
	void										adjustLoadCommands();
	void										adjustSymbolTable();
	void										adjustDATA();
	void										doLocalRelocation(const macho_relocation_info<P>* reloc);
	pint_t*										mappedAddressForVMAddress(uint32_t vmaddress);
	void										rebaseAt(int segIndex, uint64_t offset, uint8_t type);
	
	const macho_header<P>*						fHeader;
	pint_t										fOrignalVMRelocBaseAddress;
	pint_t										fSlide;
	std::vector<vmmap>							fVMMApping;
};



class MultiArchRebaser
{
public:
												MultiArchRebaser(const char* path, bool writable=false);
												~MultiArchRebaser();

	const std::vector<AbstractRebaser*>&		getArchs() const { return fRebasers; }
	void										commit();

private:
	std::vector<AbstractRebaser*>				fRebasers;
	void*										fMappingAddress;
	uint64_t									fFileSize;
};



MultiArchRebaser::MultiArchRebaser(const char* path, bool writable)
 : fMappingAddress(0), fFileSize(0)
{
	// map in whole file
	int fd = ::open(path, (writable ? O_RDWR : O_RDONLY), 0);
	if ( fd == -1 )
		throwf("can't open file %s, errno=%d", path, errno);
	struct stat stat_buf;
	if ( fstat(fd, &stat_buf) == -1)
		throwf("can't stat open file %s, errno=%d", path, errno);
	if ( stat_buf.st_size < 20 )
		throwf("file too small %s", path);
	const int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
	const int flags = writable ? (MAP_FILE | MAP_SHARED) : (MAP_FILE | MAP_PRIVATE);
	uint8_t* p = (uint8_t*)::mmap(NULL, stat_buf.st_size, prot, flags, fd, 0);
	if ( p == (uint8_t*)(-1) )
		throwf("can't map file %s, errno=%d", path, errno);
	::close(fd);

	// if fat file, process each architecture
	const fat_header* fh = (fat_header*)p;
	const mach_header* mh = (mach_header*)p;
	if ( fh->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
		// Fat header is always big-endian
		const struct fat_arch* archs = (struct fat_arch*)(p + sizeof(struct fat_header));
		for (unsigned long i=0; i < OSSwapBigToHostInt32(fh->nfat_arch); ++i) {
			uint32_t fileOffset = OSSwapBigToHostInt32(archs[i].offset);
			try {
				switch ( OSSwapBigToHostInt32(archs[i].cputype) ) {
					case CPU_TYPE_POWERPC:
						fRebasers.push_back(new Rebaser<ppc>(&p[fileOffset]));
						break;
					case CPU_TYPE_POWERPC64:
						fRebasers.push_back(new Rebaser<ppc64>(&p[fileOffset]));
						break;
					case CPU_TYPE_I386:
						fRebasers.push_back(new Rebaser<x86>(&p[fileOffset]));
						break;
					case CPU_TYPE_X86_64:
						fRebasers.push_back(new Rebaser<x86_64>(&p[fileOffset]));
						break;
					case CPU_TYPE_ARM:
						fRebasers.push_back(new Rebaser<arm>(&p[fileOffset]));
						break;
					default:
						throw "unknown file format";
				}
			}
			catch (const char* msg) {
				fprintf(stderr, "rebase warning: %s for %s\n", msg, path);
			}
		}
	}
	else {
		try {
			if ( (OSSwapBigToHostInt32(mh->magic) == MH_MAGIC) && (OSSwapBigToHostInt32(mh->cputype) == CPU_TYPE_POWERPC)) {
				fRebasers.push_back(new Rebaser<ppc>(mh));
			}
			else if ( (OSSwapBigToHostInt32(mh->magic) == MH_MAGIC_64) && (OSSwapBigToHostInt32(mh->cputype) == CPU_TYPE_POWERPC64)) {
				fRebasers.push_back(new Rebaser<ppc64>(mh));
			}
			else if ( (OSSwapLittleToHostInt32(mh->magic) == MH_MAGIC) && (OSSwapLittleToHostInt32(mh->cputype) == CPU_TYPE_I386)) {
				fRebasers.push_back(new Rebaser<x86>(mh));
			}
			else if ( (OSSwapLittleToHostInt32(mh->magic) == MH_MAGIC_64) && (OSSwapLittleToHostInt32(mh->cputype) == CPU_TYPE_X86_64)) {
				fRebasers.push_back(new Rebaser<x86_64>(mh));
			}
			else if ( (OSSwapLittleToHostInt32(mh->magic) == MH_MAGIC) && (OSSwapLittleToHostInt32(mh->cputype) == CPU_TYPE_ARM)) {
				fRebasers.push_back(new Rebaser<arm>(mh));
			}
			else {
				throw "unknown file format";
			}
		}
		catch (const char* msg) {
			fprintf(stderr, "rebase warning: %s for %s\n", msg, path);
		}
	}
	
	fMappingAddress = p;
	fFileSize = stat_buf.st_size;
}


MultiArchRebaser::~MultiArchRebaser()
{
	::munmap(fMappingAddress, fFileSize);
}

void MultiArchRebaser::commit()
{
	::msync(fMappingAddress, fFileSize, MS_ASYNC); 
}



template <typename A>
Rebaser<A>::Rebaser(const void* machHeader)
 : 	fHeader((const macho_header<P>*)machHeader)
{
	switch ( fHeader->filetype() ) {
		case MH_DYLIB:
			if ( (fHeader->flags() & MH_SPLIT_SEGS) != 0 )
				throw "split-seg dylibs cannot be rebased";
			break;
		case MH_BUNDLE:
			break;
		default:
			throw "file is not a dylib or bundle";
	}
		
}

template <> cpu_type_t Rebaser<ppc>::getArchitecture()    const { return CPU_TYPE_POWERPC; }
template <> cpu_type_t Rebaser<ppc64>::getArchitecture()  const { return CPU_TYPE_POWERPC64; }
template <> cpu_type_t Rebaser<x86>::getArchitecture()    const { return CPU_TYPE_I386; }
template <> cpu_type_t Rebaser<x86_64>::getArchitecture() const { return CPU_TYPE_X86_64; }
template <> cpu_type_t Rebaser<arm>::getArchitecture() const { return CPU_TYPE_ARM; }

template <typename A>
uint64_t Rebaser<A>::getBaseAddress() const
{
	uint64_t lowestSegmentAddress = LLONG_MAX;
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( segCmd->vmaddr() < lowestSegmentAddress ) {
				lowestSegmentAddress = segCmd->vmaddr();
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	return lowestSegmentAddress;
}

template <typename A>
uint64_t Rebaser<A>::getVMSize() const
{
	const macho_segment_command<P>* highestSegmentCmd = NULL; 
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( (highestSegmentCmd == NULL) || (segCmd->vmaddr() > highestSegmentCmd->vmaddr()) ) {
				highestSegmentCmd = segCmd;
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	
	return ((highestSegmentCmd->vmaddr() + highestSegmentCmd->vmsize() - this->getBaseAddress() + 4095) & (-4096));
}


template <typename A>
void Rebaser<A>::setBaseAddress(uint64_t addr)
{
	// calculate slide
	fSlide = addr - this->getBaseAddress();
	
	// compute base address for relocations
	this->setRelocBase();
	
	// build cache of section index to section 
	this->buildSectionTable();
		
	// update load commands
	this->adjustLoadCommands();
	
	// update symbol table  
	this->adjustSymbolTable();
	
	// update writable segments that have internal pointers
	this->adjustDATA();
}

template <typename A>
void Rebaser<A>::adjustLoadCommands()
{
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch ( cmd->cmd() ) {
			case LC_ID_DYLIB:
				if ( (fHeader->flags() & MH_PREBOUND) != 0 ) {
					// clear timestamp so that any prebound clients are invalidated
					macho_dylib_command<P>* dylib  = (macho_dylib_command<P>*)cmd;
					dylib->set_timestamp(1);
				}
				break;
			case LC_LOAD_DYLIB:
			case LC_LOAD_WEAK_DYLIB:
			case LC_REEXPORT_DYLIB:
			case LC_LOAD_UPWARD_DYLIB:
				if ( (fHeader->flags() & MH_PREBOUND) != 0 ) {
					// clear expected timestamps so that this image will load with invalid prebinding 
					macho_dylib_command<P>* dylib  = (macho_dylib_command<P>*)cmd;
					dylib->set_timestamp(2);
				}
				break;
			case macho_routines_command<P>::CMD:
				// update -init command
				{
					macho_routines_command<P>* routines = (macho_routines_command<P>*)cmd;
					routines->set_init_address(routines->init_address() + fSlide);
				}
				break;
			case macho_segment_command<P>::CMD:
				// update segment commands
				{
					macho_segment_command<P>* seg = (macho_segment_command<P>*)cmd;
					seg->set_vmaddr(seg->vmaddr() + fSlide);
					macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)seg + sizeof(macho_segment_command<P>));
					macho_section<P>* const sectionsEnd = &sectionsStart[seg->nsects()];
					for(macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
						sect->set_addr(sect->addr() + fSlide);
					}
				}
				break;
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
}


template <typename A>
void Rebaser<A>::buildSectionTable()
{
	// build vector of sections
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* seg = (macho_segment_command<P>*)cmd;
			vmmap mapping;
			mapping.vmaddr = seg->vmaddr();
			mapping.vmsize = seg->vmsize();
			mapping.fileoff = seg->fileoff();
			fVMMApping.push_back(mapping);
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}	
}


template <typename A>
void Rebaser<A>::adjustSymbolTable()
{
	const macho_dysymtab_command<P>* dysymtab = NULL;
	macho_nlist<P>* symbolTable = NULL;
	const char* strings = NULL;

	// get symbol table info
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
			case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					symbolTable = (macho_nlist<P>*)(((uint8_t*)fHeader) + symtab->symoff());
					strings = (char*)(((uint8_t*)fHeader) + symtab->stroff());
                }    
				break;
			case LC_DYSYMTAB:
				dysymtab = (macho_dysymtab_command<P>*)cmd;
				break;
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}	

	// walk all exports and slide their n_value
	macho_nlist<P>* lastExport = &symbolTable[dysymtab->iextdefsym()+dysymtab->nextdefsym()];
	for (macho_nlist<P>* entry = &symbolTable[dysymtab->iextdefsym()]; entry < lastExport; ++entry) {
		if ( (entry->n_type() & N_TYPE) == N_SECT )
			entry->set_n_value(entry->n_value() + fSlide);
	}

	// walk all local symbols and slide their n_value
	macho_nlist<P>* lastLocal = &symbolTable[dysymtab->ilocalsym()+dysymtab->nlocalsym()];
	for (macho_nlist<P>* entry = &symbolTable[dysymtab->ilocalsym()]; entry < lastLocal; ++entry) {
		if ( ((entry->n_type() & N_STAB) == 0) && ((entry->n_type() & N_TYPE) == N_SECT) ) {
			entry->set_n_value(entry->n_value() + fSlide);
		}
		else if ( entry->n_type() & N_STAB ) {
			// some stabs need to be slid too
			switch ( entry->n_type() ) {
				case N_FUN:
					// don't slide end-of-function FUN which is FUN with no string
					if ( (entry->n_strx() == 0) || (strings[entry->n_strx()] == '\0') )
						break;
				case N_BNSYM:
				case N_STSYM:
				case N_LCSYM:
					entry->set_n_value(entry->n_value() + fSlide);
					break;
			}
		}
	}
	
	// FIXME еее adjust dylib_module if it exists
}

static uint64_t read_uleb128(const uint8_t*& p, const uint8_t* end)
{
	uint64_t result = 0;
	int		 bit = 0;
	do {
		if (p == end)
			throwf("malformed uleb128");

		uint64_t slice = *p & 0x7f;

		if (bit >= 64 || slice << bit >> bit != slice)
			throwf("uleb128 too big");
		else {
			result |= (slice << bit);
			bit += 7;
		}
	} 
	while (*p++ & 0x80);
	return result;
}

template <typename A>
void Rebaser<A>::rebaseAt(int segIndex, uint64_t offset, uint8_t type)
{
	//fprintf(stderr, "rebaseAt(seg=%d, offset=0x%08llX, type=%d\n", segIndex, offset, type);
	static int lastSegIndex = -1;
	static uint8_t* lastSegMappedStart = NULL;
	if ( segIndex != lastSegIndex ) {
		const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
		const uint32_t cmd_count = fHeader->ncmds();
		const macho_load_command<P>* cmd = cmds;
		int segCount = 0;
		for (uint32_t i = 0; i < cmd_count; ++i) {
			if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
				if ( segIndex == segCount ) {
					const macho_segment_command<P>* seg = (macho_segment_command<P>*)cmd;
					lastSegMappedStart = (uint8_t*)fHeader + seg->fileoff();
					lastSegIndex = segCount;
					break;
				}
				++segCount;
			}
			cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
		}	
	}
	
	pint_t* locationToFix = (pint_t*)(lastSegMappedStart+offset);
	uint32_t* locationToFix32 = (uint32_t*)(lastSegMappedStart+offset);
	switch (type) {
		case REBASE_TYPE_POINTER:
			P::setP(*locationToFix, A::P::getP(*locationToFix) + fSlide);
			break;
		case REBASE_TYPE_TEXT_ABSOLUTE32:
			E::set32(*locationToFix32, E::get32(*locationToFix32) + fSlide);
			break;
		default:
			throwf("bad rebase type %d", type);
	}
}


template <typename A>
void Rebaser<A>::adjustDATA()
{
	const macho_dysymtab_command<P>* dysymtab = NULL;
	const macho_dyld_info_command<P>* dyldInfo = NULL;

	// get symbol table info
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch (cmd->cmd()) {
			case LC_DYSYMTAB:
				dysymtab = (macho_dysymtab_command<P>*)cmd;
				break;
			case LC_DYLD_INFO:
			case LC_DYLD_INFO_ONLY:
				dyldInfo = (macho_dyld_info_command<P>*)cmd;
				break;
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}	

	// use new encoding of rebase info if present
	if ( dyldInfo != NULL ) {
		if ( dyldInfo->rebase_size() != 0 ) {
			const uint8_t* p = (uint8_t*)fHeader + dyldInfo->rebase_off();
			const uint8_t* end = &p[dyldInfo->rebase_size()];
			
			uint8_t type = 0;
			uint64_t offset = 0;
			uint32_t count;
			uint32_t skip;
			int segIndex;
			bool done = false;
			while ( !done && (p < end) ) {
				uint8_t immediate = *p & REBASE_IMMEDIATE_MASK;
				uint8_t opcode = *p & REBASE_OPCODE_MASK;
				++p;
				switch (opcode) {
					case REBASE_OPCODE_DONE:
						done = true;
						break;
					case REBASE_OPCODE_SET_TYPE_IMM:
						type = immediate;
						break;
					case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
						segIndex = immediate;
						offset = read_uleb128(p, end);
						break;
					case REBASE_OPCODE_ADD_ADDR_ULEB:
						offset += read_uleb128(p, end);
						break;
					case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
						offset += immediate*sizeof(pint_t);
						break;
					case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
						for (int i=0; i < immediate; ++i) {
							rebaseAt(segIndex, offset, type);
							offset += sizeof(pint_t);
						}
						break;
					case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
						count = read_uleb128(p, end);
						for (uint32_t i=0; i < count; ++i) {
							rebaseAt(segIndex, offset, type);
							offset += sizeof(pint_t);
						}
						break;
					case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
						rebaseAt(segIndex, offset, type);
						offset += read_uleb128(p, end) + sizeof(pint_t);
						break;
					case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
						count = read_uleb128(p, end);
						skip = read_uleb128(p, end);
						for (uint32_t i=0; i < count; ++i) {
							rebaseAt(segIndex, offset, type);
							offset += skip + sizeof(pint_t);
						}
						break;
					default:
						throwf("bad rebase opcode %d", *p);
				}
			}	
				
		
		
		}
	}
	else {
		// walk all local relocations and slide every pointer
		const macho_relocation_info<P>* const relocsStart = (macho_relocation_info<P>*)(((uint8_t*)fHeader) + dysymtab->locreloff());
		const macho_relocation_info<P>* const relocsEnd = &relocsStart[dysymtab->nlocrel()];
		for (const macho_relocation_info<P>* reloc=relocsStart; reloc < relocsEnd; ++reloc) {
			this->doLocalRelocation(reloc);
		}
		
		// walk non-lazy-pointers and slide the ones that are LOCAL
		cmd = cmds;
		for (uint32_t i = 0; i < cmd_count; ++i) {
			if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
				const macho_segment_command<P>* seg = (macho_segment_command<P>*)cmd;
				const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)seg + sizeof(macho_segment_command<P>));
				const macho_section<P>* const sectionsEnd = &sectionsStart[seg->nsects()];
				const uint32_t* const indirectTable = (uint32_t*)(((uint8_t*)fHeader) + dysymtab->indirectsymoff());
				for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
					if ( (sect->flags() & SECTION_TYPE) == S_NON_LAZY_SYMBOL_POINTERS ) {
						const uint32_t indirectTableOffset = sect->reserved1();
						uint32_t pointerCount = sect->size() / sizeof(pint_t);
						pint_t* nonLazyPointer = (pint_t*)(((uint8_t*)fHeader) + sect->offset());
						for (uint32_t i=0; i < pointerCount; ++i, ++nonLazyPointer) {
							if ( E::get32(indirectTable[indirectTableOffset + i]) == INDIRECT_SYMBOL_LOCAL ) {
								P::setP(*nonLazyPointer, A::P::getP(*nonLazyPointer) + fSlide);
							}
						}
					}
				}
			}
			cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
		}	
	}
}


template <typename A>
typename A::P::uint_t* Rebaser<A>::mappedAddressForVMAddress(uint32_t vmaddress)
{
	for(typename std::vector<vmmap>::iterator it = fVMMApping.begin(); it != fVMMApping.end(); ++it) {
		//fprintf(stderr, "vmaddr=0x%08lX, vmsize=0x%08lX\n", it->vmaddr, it->vmsize);
		if ( (vmaddress >= it->vmaddr) && (vmaddress < (it->vmaddr+it->vmsize)) ) {
			return (pint_t*)((vmaddress - it->vmaddr) + it->fileoff + (uint8_t*)fHeader);
		}
	}
	throwf("reloc address 0x%08X not found", vmaddress);
}


template <>
void Rebaser<x86_64>::doLocalRelocation(const macho_relocation_info<x86_64::P>* reloc)
{
	if ( reloc->r_type() == X86_64_RELOC_UNSIGNED ) {
		pint_t* addr = mappedAddressForVMAddress(reloc->r_address() + fOrignalVMRelocBaseAddress);
		P::setP(*addr, P::getP(*addr) + fSlide);
	}
	else {
		throw "invalid relocation type";
	}
}

template <>
void Rebaser<ppc>::doLocalRelocation(const macho_relocation_info<P>* reloc)
{
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		if ( reloc->r_type() == GENERIC_RELOC_VANILLA ) {
			pint_t* addr = mappedAddressForVMAddress(reloc->r_address() + fOrignalVMRelocBaseAddress);
			P::setP(*addr, P::getP(*addr) + fSlide);
		}
	}
	else {
		throw "cannot rebase final linked image with scattered relocations";
	}
}

template <>
void Rebaser<x86>::doLocalRelocation(const macho_relocation_info<P>* reloc)
{
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		if ( reloc->r_type() == GENERIC_RELOC_VANILLA ) {
			pint_t* addr = mappedAddressForVMAddress(reloc->r_address() + fOrignalVMRelocBaseAddress);
			P::setP(*addr, P::getP(*addr) + fSlide);
		}
	}
	else {
		macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		if ( sreloc->r_type() == GENERIC_RELOC_PB_LA_PTR ) {
			sreloc->set_r_value( sreloc->r_value() + fSlide );
		}
		else {
			throw "cannot rebase final linked image with scattered relocations";
		}
	}
}

#if SUPPORT_ARCH_arm_any
template <>
void Rebaser<arm>::doLocalRelocation(const macho_relocation_info<P>* reloc)
{
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		if ( reloc->r_type() == ARM_RELOC_VANILLA ) {
			pint_t* addr = mappedAddressForVMAddress(reloc->r_address() + fOrignalVMRelocBaseAddress);
			P::setP(*addr, P::getP(*addr) + fSlide);
		}
	}
	else {
		macho_scattered_relocation_info<P>* sreloc = (macho_scattered_relocation_info<P>*)reloc;
		if ( sreloc->r_type() == ARM_RELOC_PB_LA_PTR ) {
			sreloc->set_r_value( sreloc->r_value() + fSlide );
		}
		else {
			throw "cannot rebase final linked image with scattered relocations";
		}
	}
}
#endif

template <typename A>
void Rebaser<A>::doLocalRelocation(const macho_relocation_info<P>* reloc)
{
	if ( (reloc->r_address() & R_SCATTERED) == 0 ) {
		if ( reloc->r_type() == GENERIC_RELOC_VANILLA ) {
			pint_t* addr = mappedAddressForVMAddress(reloc->r_address() + fOrignalVMRelocBaseAddress);
			P::setP(*addr, P::getP(*addr) + fSlide);
		}
	}
	else {
		throw "cannot rebase final linked image with scattered relocations";
	}
}


template <typename A>
void Rebaser<A>::setRelocBase()
{
	// reloc addresses are from the start of the mapped file (base address)
	fOrignalVMRelocBaseAddress = this->getBaseAddress();
	//fprintf(stderr, "fOrignalVMRelocBaseAddress=0x%08X\n", fOrignalVMRelocBaseAddress);
}

template <>
void Rebaser<ppc64>::setRelocBase()
{
	// reloc addresses either:
	// 1) from the base address if no writable segment is > 4GB from base address
	// 2) from start of first writable segment
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( segCmd->initprot() & VM_PROT_WRITE ) {
				if ( (segCmd->vmaddr() + segCmd->vmsize() - this->getBaseAddress()) > 0x100000000ULL ) {
					// found writable segment with address > 4GB past base address
					fOrignalVMRelocBaseAddress = segCmd->vmaddr();
					return;
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	// just use base address
	fOrignalVMRelocBaseAddress = this->getBaseAddress();
}

template <>
void Rebaser<x86_64>::setRelocBase()
{
	// reloc addresses are always based from the start of the first writable segment
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( segCmd->initprot() & VM_PROT_WRITE ) {
				fOrignalVMRelocBaseAddress = segCmd->vmaddr();
				return;
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	throw "no writable segment";
}


static void copyFile(const char* srcFile, const char* dstFile)
{
	// open files 
	int src = open(srcFile, O_RDONLY);	
	if ( src == -1 )
		throwf("can't open file %s, errno=%d", srcFile, errno);
	struct stat stat_buf;
	if ( fstat(src, &stat_buf) == -1)
		throwf("can't stat open file %s, errno=%d", srcFile, errno);
		
	// create new file with all same permissions to hold copy of dylib 
	::unlink(dstFile);
	int dst = open(dstFile, O_CREAT | O_RDWR | O_TRUNC, stat_buf.st_mode);	
	if ( dst == -1 )
		throwf("can't create temp file %s, errnor=%d", dstFile, errno);

	// mark source as "don't cache"
	(void)fcntl(src, F_NOCACHE, 1);
	// we want to cache the dst because we are about to map it in and modify it
	
	// copy permission bits
	if ( chmod(dstFile, stat_buf.st_mode & 07777) == -1 )
		throwf("can't chmod temp file %s, errno=%d", dstFile, errno);
	if ( chown(dstFile, stat_buf.st_uid, stat_buf.st_gid) == -1)
		throwf("can't chown temp file %s, errno=%d", dstFile, errno);
		  
	// copy contents
	ssize_t len;
	const uint32_t kBufferSize = 128*1024;
	static uint8_t* buffer = NULL;
	if ( buffer == NULL ) {
		vm_address_t addr = 0;
		if ( vm_allocate(mach_task_self(), &addr, kBufferSize, true /*find range*/) == KERN_SUCCESS )
			buffer = (uint8_t*)addr;
		else
			throw "can't allcoate copy buffer";
	}
	while ( (len = read(src, buffer, kBufferSize)) > 0 ) {
		if ( write(dst, buffer, len) == -1 )
			throwf("write failure copying feil %s, errno=%d", dstFile, errno);
	}
		
	// close files 
	int result1 = close(dst);
	int result2 = close(src);
	if ( (result1 != 0) || (result2 != 0) )
		throw "can't close file";
}


// scan dylibs and collect size info
// calculate new base address for each dylib
// rebase each file
//		copy to temp and mmap
//		update content
//		unmap/flush
//		rename

struct archInfo {
	cpu_type_t	arch;
	uint64_t	vmSize;
	uint64_t	orgBase;
	uint64_t	newBase;
};

struct fileInfo
{
	fileInfo(const char* p) : path(p) {}
	
	const char*				path;
	std::vector<archInfo>	archs;
};

//
// add archInfos to fileInfo for every slice of a fat file
// for ppc, there may be duplicate architectures (with different sub-types)
//
static void setSizes(fileInfo& info, const std::set<cpu_type_t>& onlyArchs)
{
	const MultiArchRebaser mar(info.path);
	const std::vector<AbstractRebaser*>&	rebasers = mar.getArchs();
	for(std::set<cpu_type_t>::iterator ait=onlyArchs.begin(); ait != onlyArchs.end(); ++ait) {
		for(std::vector<AbstractRebaser*>::const_iterator rit=rebasers.begin(); rit != rebasers.end(); ++rit) {
			AbstractRebaser* rebaser = *rit;
			if ( rebaser->getArchitecture() == *ait ) {
				archInfo ai;
				ai.arch = *ait;
				ai.vmSize = rebaser->getVMSize();
				ai.orgBase = rebaser->getBaseAddress();
				ai.newBase = 0;
				//fprintf(stderr, "base=0x%llX, size=0x%llX\n", ai.orgBase, ai.vmSize);
				info.archs.push_back(ai);
			}
		}
	}
}

static const char* nameForArch(cpu_type_t arch)
{
	switch( arch ) {
		case CPU_TYPE_POWERPC:
			return "ppc";
		case CPU_TYPE_POWERPC64:
			return "ppca64";
		case CPU_TYPE_I386:
			return "i386";
		case CPU_TYPE_X86_64:
			return "x86_64";
		case CPU_TYPE_ARM:
			return "arm";
	}
	return "unknown";
}

static void rebase(const fileInfo& info)
{
	// generate temp file name
	char realFilePath[PATH_MAX];
	if ( realpath(info.path, realFilePath) == NULL ) {
		throwf("realpath() failed on %s, errno=%d", info.path, errno);
	}
	const char* tempPath;
	asprintf((char**)&tempPath, "%s_rebase", realFilePath);

	// copy whole file to temp file 
	copyFile(info.path, tempPath);
	
	try {
		// rebase temp file
		MultiArchRebaser mar(tempPath, true);
		const std::vector<AbstractRebaser*>&	rebasers = mar.getArchs();
		for(std::vector<archInfo>::const_iterator fait=info.archs.begin(); fait != info.archs.end(); ++fait) {
			for(std::vector<AbstractRebaser*>::const_iterator rit=rebasers.begin(); rit != rebasers.end(); ++rit) {
				if ( (*rit)->getArchitecture() == fait->arch ) {
					(*rit)->setBaseAddress(fait->newBase);
					if ( verbose )
						printf("%8s 0x%0llX -> 0x%0llX  %s\n", nameForArch(fait->arch), fait->orgBase, fait->newBase, info.path);
				}
			}	
		}
		
		// flush temp file out to disk
		mar.commit();
		
		// rename
		int result = rename(tempPath, info.path);
		if ( result != 0 ) {
			throwf("can't swap temporary rebased file: rename(%s,%s) returned errno=%d", tempPath, info.path, errno);
		}
		
		// make sure every really gets out to disk
		::sync();
	}
	catch (const char* msg) {
		// delete temp file
		::unlink(tempPath);
		
		// throw exception with file name added
		const char* newMsg;
		asprintf((char**)&newMsg, "%s for file %s", msg, info.path);
		throw newMsg;
	}
}

static uint64_t totalVMSize(cpu_type_t arch, std::vector<fileInfo>& files)
{
	uint64_t totalSize = 0;
	for(std::vector<fileInfo>::iterator fit=files.begin(); fit != files.end(); ++fit) {
		fileInfo& fi = *fit;
		for(std::vector<archInfo>::iterator fait=fi.archs.begin(); fait != fi.archs.end(); ++fait) {
			if ( fait->arch == arch )
				totalSize += fait->vmSize;
		}
	}	
	return totalSize;
}

static uint64_t startAddress(cpu_type_t arch, std::vector<fileInfo>& files, uint64_t lowAddress, uint64_t highAddress)
{
	if ( lowAddress != 0 ) 
		return lowAddress;
	else if ( highAddress != 0 ) {
		uint64_t totalSize = totalVMSize(arch, files);
		if ( highAddress < totalSize )
			throwf("cannot use -high_address 0x%X because total size of images is greater: 0x%X", highAddress, totalSize);
		return highAddress - totalSize;
	}
	else {
		if ( (arch == CPU_TYPE_I386) || (arch == CPU_TYPE_POWERPC) ) {
			// place dylibs below dyld
			uint64_t topAddr = 0x8FE00000;
			uint64_t totalSize = totalVMSize(arch, files);
			if ( totalSize > topAddr )
				throwf("total size of images (0x%X) does not fit below 0x8FE00000", totalSize);
			return topAddr - totalSize;
		}
		else if ( arch == CPU_TYPE_POWERPC64 ) {
			return 0x200000000ULL;
		}
		else if ( arch == CPU_TYPE_X86_64 ) {
			return 0x200000000ULL;
		}
		else if ( arch == CPU_TYPE_ARM ) {
			// place dylibs below dyld
			uint64_t topAddr = 0x2FE00000;
			uint64_t totalSize = totalVMSize(arch, files);
			if ( totalSize > topAddr )
				throwf("total size of images (0x%X) does not fit below 0x2FE00000", totalSize);
			return topAddr - totalSize;
		}
		else
			throw "unknown architecture";
	}
}

static void usage()
{
	fprintf(stderr, "rebase [-low_address] [-high_address] [-v] [-arch <arch>] files...\n");
}


int main(int argc, const char* argv[])
{
	std::vector<fileInfo> files;
	std::set<cpu_type_t> onlyArchs;
	uint64_t lowAddress = 0;
	uint64_t highAddress = 0;

	try {
		// parse command line options
		char* endptr;
		for(int i=1; i < argc; ++i) {
			const char* arg = argv[i];
			if ( arg[0] == '-' ) {
				if ( strcmp(arg, "-v") == 0 ) {
					verbose = true;
				}
				else if ( strcmp(arg, "-low_address") == 0 ) {
					lowAddress = strtoull(argv[++i], &endptr, 16);
				}
				else if ( strcmp(arg, "-high_address") == 0 ) {
					highAddress = strtoull(argv[++i], &endptr, 16);
				}
				else if ( strcmp(arg, "-arch") == 0 ) {
					const char* archName = argv[++i];
					if ( archName == NULL )
						throw "-arch missing architecture name";
					bool found = false;
					for (const ArchInfo* t=archInfoArray; t->archName != NULL; ++t) {
						if ( strcmp(t->archName,archName) == 0 ) {
							onlyArchs.insert(t->cpuType);
							found = true;
						}
					}
					if ( !found )
						throwf("unknown architecture %s", archName);
				}
				else {
					usage();
					throwf("unknown option: %s\n", arg);
				}
			}
			else {
				files.push_back(fileInfo(arg));
			}
		}
		
		if ( files.size() == 0 )
			throw "no files specified";
		
		// use all architectures if no restrictions specified
		if ( onlyArchs.size() == 0 ) {
			onlyArchs.insert(CPU_TYPE_POWERPC);
			onlyArchs.insert(CPU_TYPE_POWERPC64);
			onlyArchs.insert(CPU_TYPE_I386);
			onlyArchs.insert(CPU_TYPE_X86_64);
			onlyArchs.insert(CPU_TYPE_ARM);
		}
		
		// scan files and collect sizes
		for(std::vector<fileInfo>::iterator it=files.begin(); it != files.end(); ++it) {
			setSizes(*it, onlyArchs);
		}
				
		// assign new base address for each arch
		for(std::set<cpu_type_t>::iterator ait=onlyArchs.begin(); ait != onlyArchs.end(); ++ait) {
			cpu_type_t arch = *ait;
			uint64_t baseAddress = startAddress(arch, files, lowAddress, highAddress);
			for(std::vector<fileInfo>::iterator fit=files.begin(); fit != files.end(); ++fit) {
				fileInfo& fi = *fit;
				for(std::vector<archInfo>::iterator fait=fi.archs.begin(); fait != fi.archs.end(); ++fait) {
					if ( fait->arch == arch ) {
						fait->newBase = baseAddress;
						baseAddress += fait->vmSize; 
						baseAddress = (baseAddress + 4095) & (-4096);  // page align
					}
				}
			}
		}
		
		// rebase each file if it contains something rebaseable
		for(std::vector<fileInfo>::iterator it=files.begin(); it != files.end(); ++it) {
			fileInfo& fi = *it;
			if ( fi.archs.size() > 0 )
				rebase(fi);
		}
		
	}
	catch (const char* msg) {
		fprintf(stderr, "rebase failed: %s\n", msg);
		return 1;
	}
	
	return 0;
}



