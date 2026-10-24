/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "host_observe.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/proc.h>
#else
#include <dirent.h>
#endif

static int compare(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int finish(int *values, size_t count, int **output, size_t *length)
{
    qsort(values, count, sizeof *values, compare);
    for (size_t i = 1; i < count; ++i) {
        if (values[i] == values[i - 1]) { free(values); errno = EIO; return -1; }
    }
    *output = values;
    *length = count;
    return 0;
}

#ifdef __APPLE__
/* libproc is deliberately confined to test support, outside SDK interfaces. */
static int process_list(pid_t pid, int descriptors, int **output, size_t *length)
{
    size_t width = descriptors ? sizeof(struct proc_fdinfo) : sizeof(pid_t);
    errno = 0;
    int needed = descriptors ? proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0)
                             : proc_listpids(PROC_PPID_ONLY, (uint32_t)pid, NULL, 0);
    if (needed < 0 || (descriptors && !needed)) return -1;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        size_t capacity = (size_t)needed + 128 * width;
        if (capacity > INT_MAX) { errno = EOVERFLOW; return -1; }
        void *buffer = calloc(1, capacity);
        if (!buffer) return -1;
        errno = 0;
        int bytes = descriptors ? proc_pidinfo(pid, PROC_PIDLISTFDS, 0, buffer, (int)capacity)
                                : proc_listpids(PROC_PPID_ONLY, (uint32_t)pid, buffer, (int)capacity);
        if (bytes < 0 || (descriptors && !bytes) || (size_t)bytes % width) {
            free(buffer); if (!errno) errno = EIO; return -1;
        }
        if ((size_t)bytes >= capacity) { free(buffer); needed = bytes; continue; }
        size_t count = (size_t)bytes / width, used = 0;
        int *values = malloc((count ? count : 1) * sizeof *values);
        if (!values) { free(buffer); return -1; }
        for (size_t i = 0; i < count; ++i) {
            int value = descriptors ? ((struct proc_fdinfo *)buffer)[i].proc_fd
                                    : ((pid_t *)buffer)[i];
            if (descriptors || value > 0) values[used++] = value;
        }
        free(buffer);
        return finish(values, used, output, length);
    }
    errno = EAGAIN;
    return -1;
}

int beam_test_host_fds(pid_t pid, int **values, size_t *count)
{ return process_list(pid, 1, values, count); }

int beam_test_host_children(pid_t pid, int **values, size_t *count)
{ return process_list(pid, 0, values, count); }

int beam_test_host_threads(pid_t pid)
{
    if (pid != getpid()) {
        struct proc_taskinfo info;
        if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof info) != sizeof info) return -1;
        return info.pti_threadnum;
    }
    thread_act_array_t threads;
    mach_msg_type_number_t count;
    kern_return_t result = task_threads(mach_task_self(), &threads, &count);
    if (result != KERN_SUCCESS) { errno = EIO; return -1; }
    int failed = 0;
    for (mach_msg_type_number_t i = 0; i < count; ++i)
        if (mach_port_deallocate(mach_task_self(), threads[i]) != KERN_SUCCESS) failed = 1;
    if (vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof *threads) != KERN_SUCCESS)
        failed = 1;
    if (failed || count > INT_MAX) { errno = EIO; return -1; }
    return (int)count;
}

int beam_test_host_executable_regions(void)
{
    mach_vm_address_t address = 0;
    natural_t depth = 0;
    int regions = 0;
    for (;;) {
        mach_vm_size_t size = 0;
        vm_region_submap_info_data_64_t info;
        /* Only v0 fields are needed; newer SDKs can outpace the running OS. */
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_V0_COUNT_64;
        kern_return_t status = mach_vm_region_recurse(mach_task_self(), &address, &size,
            &depth, (vm_region_recurse_info_t)&info, &count);
        if (status == KERN_INVALID_ADDRESS) return regions;
        if (status != KERN_SUCCESS || count != VM_REGION_SUBMAP_INFO_V0_COUNT_64 || !size) {
            errno = EIO; return -1;
        }
        if (info.is_submap) { ++depth; continue; }
        if (info.protection & VM_PROT_EXECUTE) {
            if (regions == INT_MAX) { errno = EOVERFLOW; return -1; }
            ++regions;
        }
        if (address > UINT64_MAX - size) { errno = EOVERFLOW; return -1; }
        address += size;
    }
}

void beam_test_host_free_paths(char **paths, size_t count)
{
    for (size_t i=0;i<count;++i) free(paths[i]);
    free(paths);
}

int beam_test_host_mapped_files(pid_t pid, char ***paths, size_t *length)
{
    char executable[PATH_MAX];
    char **found=NULL;
    size_t count=0,capacity=0;
    uint64_t address=0;
    unsigned regions=0;
    int executable_seen=0;
    if (beam_test_host_executable(pid,executable,sizeof executable)) return -1;
    for (;regions<100000;++regions) {
        struct proc_regionwithpathinfo info;
        errno=0;
        int bytes=proc_pidinfo(pid,PROC_PIDREGIONPATHINFO,address,&info,sizeof info);
        if (!bytes && errno==EINVAL && regions && executable_seen) {
            *paths=found; *length=count; return 0;
        }
        if (bytes!=sizeof info) { if (!errno) errno=EIO; goto failed; }
        uint64_t start=info.prp_prinfo.pri_address,size=info.prp_prinfo.pri_size;
        if (!size || start>UINT64_MAX-size || start+size<=address) { errno=EIO; goto failed; }
        address=start+size;
        const char *path=info.prp_vip.vip_path;
        if (!memchr(path,0,sizeof info.prp_vip.vip_path)) { errno=ENAMETOOLONG; goto failed; }
        if (!*path) continue;
        if (!strcmp(path,executable)) executable_seen=1;
        size_t i;
        for (i=0;i<count;++i) if (!strcmp(path,found[i])) break;
        if (i<count) continue;
        if (count==8192) { errno=EOVERFLOW; goto failed; }
        if (count==capacity) {
            capacity=capacity ? capacity*2 : 32;
            char **grown=realloc(found,capacity*sizeof(*found));
            if (!grown) goto failed;
            found=grown;
        }
        found[count]=strdup(path);
        if (!found[count]) goto failed;
        count++;
    }
    errno=EOVERFLOW;
failed:
    beam_test_host_free_paths(found,count);
    return -1;
}

int beam_test_host_runtime_images(void)
{
    uint32_t total = _dyld_image_count();
    int found = 0;
    if (!total) { errno = EIO; return -1; }
    for (uint32_t i = 0; i < total; ++i) {
        const char *name = _dyld_get_image_name(i);
        if (!name) { errno = EAGAIN; return -1; }
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        if (!strcmp(base, "libbeam.dylib")) ++found;
    }
    if (_dyld_image_count() != total) { errno = EAGAIN; return -1; }
    return found;
}

int beam_test_host_executable(pid_t pid, char *path, size_t capacity)
{
    char raw[PROC_PIDPATHINFO_MAXSIZE], canonical[PATH_MAX];
    if (proc_pidpath(pid, raw, sizeof raw) <= 0 || !realpath(raw, canonical)) return -1;
    if (strlen(canonical) >= capacity) { errno = ENAMETOOLONG; return -1; }
    strcpy(path, canonical);
    return 0;
}

int beam_test_host_state(pid_t pid, int *alive, int *zombie)
{
    struct proc_bsdshortinfo info;
    errno = 0;
    int result = proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &info, sizeof info);
    if (!result && errno == ESRCH) { *alive = *zombie = 0; return 0; }
    if (result != sizeof info) { if (!errno) errno = EIO; return -1; }
    *alive = 1;
    *zombie = info.pbsi_status == SZOMB;
    return 0;
}
#else
static int entries(const char *path, int **output, size_t *length)
{
    DIR *directory = opendir(path);
    if (!directory) return -1;
    size_t capacity = 32, count = 0;
    int *values = malloc(capacity * sizeof *values);
    if (!values) { closedir(directory); return -1; }
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        char *end;
        long value = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || value < 0 || value > INT_MAX) continue;
        if (count == capacity) {
            capacity *= 2;
            int *grown = realloc(values, capacity * sizeof *values);
            if (!grown) { free(values); closedir(directory); return -1; }
            values = grown;
        }
        values[count++] = (int)value;
    }
    int error = errno;
    closedir(directory);
    if (error) { free(values); errno = error; return -1; }
    return finish(values, count, output, length);
}

int beam_test_host_fds(pid_t pid, int **values, size_t *count)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/fd", pid);
    if (entries(path, values, count)) return -1;
    /* The directory used for enumeration is already closed. */
    if (pid == getpid()) {
        size_t used = 0;
        for (size_t i = 0; i < *count; ++i) {
            snprintf(path, sizeof path, "/proc/%d/fd/%d", pid, (*values)[i]);
            if (!access(path, F_OK)) (*values)[used++] = (*values)[i];
            else if (errno != ENOENT) { free(*values); return -1; }
        }
        *count = used;
    }
    return 0;
}

int beam_test_host_children(pid_t pid, int **values, size_t *count)
{
    char path[96];
    snprintf(path, sizeof path, "/proc/%d/task/%d/children", pid, pid);
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    size_t capacity = 32, used = 0;
    int *pids = malloc(capacity * sizeof *pids), value;
    if (!pids) { fclose(file); return -1; }
    int result;
    while ((result = fscanf(file, "%d", &value)) == 1) {
        if (used == capacity) {
            capacity *= 2;
            int *grown = realloc(pids, capacity * sizeof *pids);
            if (!grown) { free(pids); fclose(file); return -1; }
            pids = grown;
        }
        pids[used++] = value;
    }
    int error = ferror(file) || result != EOF;
    fclose(file);
    if (error) { free(pids); errno = EIO; return -1; }
    return finish(pids, used, values, count);
}

int beam_test_host_threads(pid_t pid)
{
    char path[64]; int *values; size_t count;
    snprintf(path, sizeof path, "/proc/%d/task", pid);
    if (entries(path, &values, &count)) return -1;
    free(values);
    return count <= INT_MAX ? (int)count : -1;
}

int beam_test_host_executable_regions(void)
{
    FILE *file = fopen("/proc/self/maps", "r");
    char line[8192], permissions[5]; int count = 0, error = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        if (!strchr(line, '\n') || sscanf(line, "%*s %4s", permissions) != 1) { error = 1; break; }
        if (permissions[2] == 'x') ++count;
    }
    error |= ferror(file);
    fclose(file);
    if (error) { errno = EIO; return -1; }
    return count;
}

int beam_test_host_executable(pid_t pid, char *path, size_t capacity)
{
    char source[64], canonical[PATH_MAX];
    snprintf(source, sizeof source, "/proc/%d/exe", pid);
    if (!realpath(source, canonical)) return -1;
    if (strlen(canonical) >= capacity) { errno = ENAMETOOLONG; return -1; }
    strcpy(path, canonical); return 0;
}

int beam_test_host_state(pid_t pid, int *alive, int *zombie)
{
    char path[64], line[8192];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT || errno == ESRCH) { *alive = *zombie = 0; return 0; }
        return -1;
    }
    int read = fgets(line, sizeof line, file) != NULL;
    fclose(file);
    char *end = read ? strrchr(line, ')') : NULL;
    if (!end || end[1] != ' ' || !end[2]) { errno = EIO; return -1; }
    *alive = 1; *zombie = end[2] == 'Z'; return 0;
}
#endif

int beam_test_host_image(const void *address, char *path, size_t capacity)
{
    Dl_info info; char canonical[PATH_MAX];
    if (!dladdr(address, &info) || !info.dli_fname) { errno = ENOENT; return -1; }
    if (!realpath(info.dli_fname, canonical)) return -1;
    if (strlen(canonical) >= capacity) { errno = ENAMETOOLONG; return -1; }
    strcpy(path, canonical); return 0;
}

int beam_test_host_entries(const char *path)
{
    if (!strcmp(path,"/proc/self/task")) return beam_test_host_threads(getpid());
    if (strcmp(path,"/proc/self/fd")) { errno=EINVAL; return -1; }
    int *values; size_t count;
    if (beam_test_host_fds(getpid(),&values,&count)) return -1;
    free(values);
    if (count>INT_MAX) { errno=EOVERFLOW; return -1; }
    return (int)count;
}
int beam_test_host_copy_fds(int *values, size_t capacity)
{
    int *found; size_t count;
    if (beam_test_host_fds(getpid(),&found,&count)) return -1;
    if (count>capacity || count>INT_MAX) { free(found); errno=EOVERFLOW; return -1; }
    memcpy(values,found,count*sizeof(*values)); free(found);
    return (int)count;
}
int beam_test_host_child_text(char *text, size_t capacity)
{
    int *found; size_t count,used=0;
    if (!capacity) { errno=EINVAL; return -1; }
    if (beam_test_host_children(getpid(),&found,&count)) return -1;
    text[0]=0;
    for (size_t i=0;i<count;++i) {
        int length=snprintf(text+used,capacity-used,"%d ",found[i]);
        if (length<0 || (size_t)length>=capacity-used) { free(found); errno=EOVERFLOW; return -1; }
        used+=(size_t)length;
    }
    free(found); return 0;
}
