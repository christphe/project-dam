/*
 * ge_pkg.h -- mod packages, in ge_assets (issue #139): a mod is a `.pmod`, a plain uncompressed ustar tar of its source folder.
 * A texture pack is a `.ptexpack`, the same kind of tar (pack.json + textures/<texnum>.png); everything below about
 * ".pmod" holds for ".ptexpack" too.
 *
 * One read layer for everything the game, the editor and the tests read from a mod. A path is an ordinary path unless
 * one of its components ends in ".pmod" and the path up to there is a regular FILE: then the rest of the path names an
 * entry inside that tar ("<mods>/formula.pmod/campaign.json"). When the ".pmod" component is a DIRECTORY (an unpacked
 * package) or there is none, the path is read as an ordinary file, so a folder named X.pmod/ behaves exactly like the
 * tar X.pmod. Both '/' and '\' separate components. The index of the last few packages is cached (keyed by path, size
 * and mtime), so repeated reads do not rescan.
 *
 * Not thread-safe (one index cache): call it from one thread at a time.
 */
#ifndef GE_PKG_H
#define GE_PKG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read the whole file: *out is a malloc'd buffer of *len bytes plus one extra 0 byte (so it is a C string for text).
 * 1 on success, 0 if the file does not exist (inside the package) or cannot be read. Free with pkgFree. */
int pkgReadFile(const char *path, uint8_t **out, size_t *len);

/* 1 if `path` names a readable regular file, in a package or not. */
int pkgExists(const char *path);

void pkgFree(void *p);

/* Every regular-file entry of the package file `pkgFile` (a tar, not a folder): cb(u, name, offset of its data in the
 * file, size). Built afresh, without the shared index cache, so the caller may keep the offsets and read them from another
 * thread with its own FILE. 1 on success, 0 if it is not a readable tar. */
int pkgForEachEntry(const char *pkgFile, void (*cb)(void *u, const char *name, long long off, long long size), void *u);

/* Tar the folder `srcDir` into `outFile` (written to outFile.tmp, then renamed over it). Recursive, sorted, regular
 * files only; dotfiles and dot-folders (.git, .gitignore) and a top-level dist/ are skipped; times and owners are
 * zeroed so the same tree gives the same bytes. 1 on success; else 0 with a message in err. *outBytes (may be NULL) is
 * the size written. */
int pkgWriteDir(const char *srcDir, const char *outFile, char *err, size_t errlen);
int pkgWriteDirSized(const char *srcDir, const char *outFile, uint64_t *outBytes, char *err, size_t errlen);

/* Unpack the package file `pkgFile` into the new folder `destDir` (it must not exist; its parent must). Every regular
 * file of the tar, with its subfolders. An entry that would land outside destDir (an absolute path, a drive, a ".."
 * component) refuses the whole package. 1 on success; else 0 with a message in err, and destDir may hold part of the
 * files (the caller removes it). */
int pkgExtract(const char *pkgFile, const char *destDir, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif
