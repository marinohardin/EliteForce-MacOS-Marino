/*
===========================================================================
macOS app bundle: the game files from GOG's Windows installer (sys_osx_gog.c).

This file is part of VoyagerSP (GPLv2 or later, like the rest of the engine).
===========================================================================
*/

#ifndef SYS_OSX_GOG_H
#define SYS_OSX_GOG_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint64_t	offset;			// of the stored (zlib) data in the installer
	uint64_t	stored, size;	// stored and unpacked sizes
	uint8_t		sha1[20];		// of the stored data (Inno's)
	uint8_t		md5[16];		// of the unpacked data (GOG's)
} gogPart_t;

typedef struct {
	char		name[64];		// in BaseEF, like pak0.pk3
	uint64_t	size;
	uint8_t		md5[16];
	int			numParts, partsFound;
	gogPart_t	*parts;
} gogFile_t;

typedef struct {
	gogFile_t	*files;			// BaseEF's .pk3 files
	int			numFiles;
} gogInstaller_t;

// Reads GOG's setup_….exe. 0 with err set to the reason (a phrase that
// follows "This installer can't be used:") when it isn't one this app knows.
int GOG_ReadInstaller( const char *path, gogInstaller_t *inst, char *err, size_t errSize );
void GOG_Free( gogInstaller_t *inst );

// Unpacks one file to <to>.part, checks it and renames it to <to>; adds the
// bytes written to *progress; stops when *cancel is set. 0 with err set on
// failure (the .part file is removed).
int GOG_ExtractFile( const char *installer, const gogFile_t *f, const char *to,
	volatile unsigned long long *progress, volatile signed char *cancel, char *err, size_t errSize );

#endif
