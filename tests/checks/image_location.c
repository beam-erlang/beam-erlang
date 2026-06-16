/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_runtime_image.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void same(const char *input,const char *expected) {
    char *path=beam_runtime_image_location(input);assert(path);assert(!strcmp(path,expected));free(path);
}
int main(void) {
    char directory[]="/tmp/beam-image-XXXXXX";assert(mkdtemp(directory));
    char original[1024],archive[1024],link[1024],location[2048],expected[2048];
    snprintf(original,sizeof original,"%s/literal!.so",directory);
    FILE *file=fopen(original,"w");assert(file);fclose(file);
    char *canonical=realpath(original,NULL);assert(canonical);same(original,canonical);free(canonical);
    snprintf(archive,sizeof archive,"%s/base.apk",directory);file=fopen(archive,"w");assert(file);fclose(file);
    snprintf(link,sizeof link,"%s/alias.apk",directory);assert(!symlink(archive,link));
    canonical=realpath(archive,NULL);assert(canonical);
    snprintf(expected,sizeof expected,"%s!/lib/arm64-v8a/libbeam.so",canonical);
    snprintf(location,sizeof location,"%s!/lib/arm64-v8a/libbeam.so",link);same(location,expected);
    const char *bad[]={"", "/libbeam.so", "../libbeam.so", "lib/../libbeam.so", "./libbeam.so", "lib//libbeam.so", "lib/", "lib\\libbeam.so", "nested.apk!/libbeam.so"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
        snprintf(location,sizeof location,"%s!/%s",archive,bad[i]);assert(!beam_runtime_image_location(location));
    }
    snprintf(location,sizeof location,"%s!/lib.so",directory);assert(!beam_runtime_image_location(location));
    /* An actual filename containing the separator wins over composite syntax. */
    char literal_dir[1024];snprintf(literal_dir,sizeof literal_dir,"%s/name!",directory);assert(!mkdir(literal_dir,0700));
    snprintf(location,sizeof location,"%s/lib.so",literal_dir);file=fopen(location,"w");assert(file);fclose(file);
    char *literal=realpath(location,NULL);same(location,literal);
    char composite[4096],canonical_composite[4096];
    snprintf(composite,sizeof composite,"%s!/lib/arm64-v8a/libbeam.so",location);
    snprintf(canonical_composite,sizeof canonical_composite,"%s!/lib/arm64-v8a/libbeam.so",literal);
    same(composite,canonical_composite);
    free(literal);unlink(location);rmdir(literal_dir);
    free(canonical);unlink(link);unlink(archive);unlink(original);rmdir(directory);return 0;
}
