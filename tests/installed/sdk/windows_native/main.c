/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/sdk/inventories.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <processsnapshot.h>
#ifdef BUILTIN_HEADER
#include BUILTIN_HEADER
#endif
#ifndef EXPECTED_ANSWER
#define EXPECTED_ANSWER 19
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"native SDK:%d: %s (%s; Windows %lu)\n", \
    __LINE__,#x,error.message,GetLastError()); goto done; } } while (0)

typedef struct handle_record { HANDLE value; DWORD type; wchar_t name[128]; } handle_record;
typedef struct observation {
    DWORD handles, threads; unsigned executable, count; handle_record inventory[256];
} observation;
typedef struct host_control { HANDLE ready,stop; } host_control;
static struct enif_entry_t *failed_initializer(void) { return NULL; }

static int restored(const observation *before, const observation *after)
{
    if (before->handles == after->handles && before->threads == after->threads &&
        before->executable == after->executable) return 1;
    fprintf(stderr,"resource restoration: handles %lu/%lu, threads %lu/%lu, executable %u/%u\n",
        before->handles,after->handles,before->threads,after->threads,before->executable,after->executable);
    for (unsigned i=0; i<after->count; ++i) {
        unsigned j;
        for (j=0; j<before->count; ++j)
            if (before->inventory[j].value == after->inventory[i].value) break;
        if (j == before->count) {
            char name[512] = {0};
            WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,after->inventory[i].name,-1,name,sizeof(name),NULL,NULL);
            fprintf(stderr,"retained handle %p type %lu name %s\n",after->inventory[i].value,after->inventory[i].type,name);
        }
    }
    return 0;
}
static unsigned __stdcall host_worker(void *argument)
{
    host_control *control = argument;
    if (!SetEvent(control->ready)) return 1;
    return WaitForSingleObject(control->stop,INFINITE) == WAIT_OBJECT_0 ? 0 : 1;
}

static int observe(observation *result)
{
    int diagnostic = GetEnvironmentVariableW(L"BEAM_TEST_NATIVE_MAPS",NULL,0) != 0;
    if (diagnostic) fputs("EXECUTABLE_SNAPSHOT\n",stderr);
    HPSS captured = NULL; HPSSWALK marker = NULL;
    DWORD code = PssCaptureSnapshot(GetCurrentProcess(),PSS_CAPTURE_HANDLES |
        PSS_CAPTURE_HANDLE_NAME_INFORMATION | PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION,0,&captured);
    if (code != ERROR_SUCCESS) { SetLastError(code); return 0; }
    code = PssWalkMarkerCreate(NULL,&marker);
    if (code != ERROR_SUCCESS) { PssFreeSnapshot(GetCurrentProcess(),captured); SetLastError(code); return 0; }
    PSS_HANDLE_ENTRY handle;
    result->count = 0;
    while ((code = PssWalkSnapshot(captured,PSS_WALK_HANDLES,marker,&handle,sizeof(handle))) == ERROR_SUCCESS) {
        if (result->count == 256) { code = ERROR_INSUFFICIENT_BUFFER; break; }
        handle_record *record = &result->inventory[result->count++];
        record->value = handle.Handle; record->type = handle.ObjectType;
        unsigned length = handle.ObjectNameLength/sizeof(wchar_t);
        if (length > 127) length = 127;
        if (length) memcpy(record->name,handle.ObjectName,length*sizeof(wchar_t));
        record->name[length] = 0;
    }
    DWORD released_marker = PssWalkMarkerFree(marker);
    DWORD released_snapshot = PssFreeSnapshot(GetCurrentProcess(),captured);
    if (code != ERROR_NO_MORE_ITEMS || released_marker || released_snapshot) { SetLastError(code); return 0; }
    if (!GetProcessHandleCount(GetCurrentProcess(),&result->handles)) return 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry = {sizeof(entry)};
    result->threads = 0;
    if (!Thread32First(snapshot,&entry)) { CloseHandle(snapshot); return 0; }
    do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++result->threads; }
    while (Thread32Next(snapshot,&entry));
    DWORD failure = GetLastError();
    if (!CloseHandle(snapshot) || failure != ERROR_NO_MORE_FILES || !result->threads) return 0;
    SYSTEM_INFO system; GetSystemInfo(&system);
    uintptr_t address = (uintptr_t)system.lpMinimumApplicationAddress;
    uintptr_t maximum = (uintptr_t)system.lpMaximumApplicationAddress;
    result->executable = 0;
    while (address <= maximum) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((const void *)address,&region,sizeof(region)) != sizeof(region)) return 0;
        DWORD protection = region.Protect & 0xff;
        if (region.State == MEM_COMMIT && (protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY)) {
            ++result->executable;
            if (diagnostic) {
                wchar_t wide[32768]; char path[32768];
                DWORD count = region.Type == MEM_IMAGE ? GetModuleFileNameW((HMODULE)region.AllocationBase,wide,32768) : 0;
                if (!count) strcpy(path,"<private>");
                else if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,path,sizeof(path),NULL,NULL)) return 0;
                fprintf(stderr,"%p %p %lu %s\n",region.BaseAddress,region.AllocationBase,region.Type,path);
            }
        }
        uintptr_t base = (uintptr_t)region.BaseAddress;
        if (!region.RegionSize || base > UINTPTR_MAX-region.RegionSize) return 0;
        uintptr_t next = base+region.RegionSize;
        if (next <= address) return 0;
        address = next;
    }
    return 1;
}

static int runtime_images(void)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    MODULEENTRY32W entry = {sizeof(entry)};
    int count = 0;
    if (!Module32FirstW(snapshot,&entry)) { CloseHandle(snapshot); return -1; }
    do {
        if (entry.hModule != GetModuleHandleW(NULL) && GetProcAddress(entry.hModule,"beam_build_id")) ++count;
    } while (Module32NextW(snapshot,&entry));
    DWORD error = GetLastError();
    if (!CloseHandle(snapshot) || error != ERROR_NO_MORE_FILES) return -1;
    return count;
}

static char *utf8(const wchar_t *wide)
{
    int size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,NULL,0,NULL,NULL);
    char *value = size ? malloc(size) : NULL;
    if (value && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,value,size,NULL,NULL)) {
        free(value); return NULL;
    }
    return value;
}

int wmain(int argc, wchar_t **argv)
{
    beam_error error = {0}; beam_system *system = NULL; beam_vm *vm = NULL;
    beam_composition *composition = NULL, *failing = NULL; beam_result result = {0};
    unsigned char *bytes = NULL, *arguments = NULL; char *origin = NULL, *native = NULL, *python = NULL; int status = 1;
    HANDLE descendant = NULL;
    PROCESS_INFORMATION host_process = {0};
    beam_module_spec modules[5] = {0};
    host_control control = {0}; HANDLE host_thread = NULL;
    CHECK(argc == 5 && beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode->count == 4);
    origin = utf8(argv[1]); native = utf8(argv[2]); CHECK(origin && native);
    python = utf8(argv[3]); CHECK(python);
    size_t python_size = strlen(python); CHECK(python_size <= 32768);
    size_t argument_size = 12+python_size;
    arguments = calloc(argument_size,1); CHECK(arguments);
    /* ETF [<<absolute selected Python path>>]. */
    arguments[0]=131; arguments[1]=108; arguments[5]=1; arguments[6]=109;
    for (unsigned i=0; i<4; ++i) arguments[7+i]=(unsigned char)(python_size >> (24-8*i));
    memcpy(arguments+11,python,python_size); arguments[11+python_size]=106;
    FILE *input = _wfopen(argv[1],L"rb"); CHECK(input);
    CHECK(!fseek(input,0,SEEK_END)); long size = ftell(input); CHECK(size > 0 && !fseek(input,0,SEEK_SET));
    bytes = malloc(size); CHECK(bytes && fread(bytes,1,size,input) == (size_t)size && !fclose(input));
    beam_module_spec module = {"pkg_acceptance",{bytes,(size_t)size},origin};
    modules[0] = module;
    for (size_t i=0; i<beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode->count; ++i) {
        const beam_bytecode_record *record = &beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode->records[i];
        char *prefix = utf8(argv[4]); CHECK(prefix);
        size_t capacity = strlen(prefix)+strlen(record->relative_path)+2;
        char *path = malloc(capacity); CHECK(path);
        snprintf(path,capacity,"%s/%s",prefix,record->relative_path); free(prefix);
        modules[i+1].name = record->name; modules[i+1].origin = path;
        int count = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
        wchar_t *wide = count ? malloc(count*sizeof(*wide)) : NULL; CHECK(wide);
        CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,count));
        FILE *file = _wfopen(wide,L"rb"); free(wide); CHECK(file);
        unsigned char *payload = malloc(record->size); CHECK(payload);
        modules[i+1].bytes = (beam_bytes){payload,record->size};
        CHECK(fread(payload,1,record->size,file) == record->size && fgetc(file) == EOF && !ferror(file));
        CHECK(!fclose(file));
    }
    CHECK(beam_bytecode_verify(beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode,modules+1,4,&error) == BEAM_OK);
    beam_nif_spec nif = {sizeof(nif),"pkg_acceptance",native};
    beam_composition_options preparation = {.struct_size=sizeof(preparation),.module_count=5,.modules=modules,
        .services=BEAM_SERVICE_PROGRAM};
#ifdef BUILTIN_HEADER
    preparation.builtin_nif_count = 1; preparation.builtin_nifs = BUILTIN_REGISTRATION();
#else
    preparation.nif_count = 1; preparation.nifs = &nif;
#endif
    CHECK(beam_composition_prepare(&preparation,&composition,&error) == BEAM_OK);
    beam_builtin_nif broken = {sizeof(broken),"pkg_acceptance",beam_build_id(),failed_initializer,0};
    beam_composition_options rejected = {.struct_size=sizeof(rejected),.module_count=1,.modules=&module,
        .builtin_nif_count=1,.builtin_nifs=&broken};
    CHECK(beam_composition_prepare(&rejected,&failing,&error) == BEAM_OK);
#ifdef STATIC_HOST
    CHECK(runtime_images() == 0);
#else
    CHECK(runtime_images() == 1);
#endif
    /* Exercise a real foreign CRT thread before the VM. Windows lazily maps
     * kernel.appcore.dll on first CRT thread creation; that system dependency
     * belongs to the host. Keep its live thread and events throughout every VM
     * cycle and require their preservation as part of the OS baseline. */
    control.ready = CreateEventW(NULL,TRUE,FALSE,NULL);
    control.stop = CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(control.ready && control.stop);
    host_thread = (HANDLE)_beginthreadex(NULL,0,host_worker,&control,0,NULL);
    CHECK(host_thread && WaitForSingleObject(control.ready,5000) == WAIT_OBJECT_0);
    /* Windows initializes process-wide compatibility registry state on the
     * first CreateProcess. Establish it through a real host-owned child, which
     * must survive all VM process-job cleanup and is stopped only by the host. */
    size_t command_capacity = wcslen(argv[3])+64;
    wchar_t *host_command = malloc(command_capacity*sizeof(*host_command)); CHECK(host_command);
    swprintf(host_command,command_capacity,L"\"%ls\" -c \"import time;time.sleep(60)\"",argv[3]);
    STARTUPINFOW host_startup = {.cb=sizeof(host_startup)};
    BOOL created = CreateProcessW(argv[3],host_command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,
                                 &host_startup,&host_process);
    free(host_command); CHECK(created);
    CHECK(CloseHandle(host_process.hThread)); host_process.hThread = NULL;
    CHECK(beam_init(&system,&error) == BEAM_OK);
    observation before,after; CHECK(observe(&before));
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        beam_vm_options options; beam_vm_options_init(&options); options.composition = failing;
        CHECK(beam_vm_init(system,&options,&vm,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(!vm && strstr(error.message,"built-in NIF initialization failed: pkg_acceptance"));
        CHECK(observe(&after) && restored(&before,&after));
        options.composition = composition;
        CHECK(beam_vm_init(system,&options,&vm,&error) == BEAM_OK);
        CHECK(beam_vm_start(vm,&error) == BEAM_OK);
        CHECK(beam_vm_activate(vm,&error) == BEAM_OK);
#ifdef BUILTIN_HEADER
        const unsigned char load[] = {131,108,0,0,0,1,107,0,7,'b','u','i','l','t','i','n',106};
        CHECK(beam_vm_call(vm,"pkg_acceptance","load",(beam_bytes){load,sizeof(load)},&result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN); beam_result_release(&result);
#endif
        const unsigned char nil[] = {131,106};
        CHECK(beam_vm_call(vm,"pkg_acceptance","value",(beam_bytes){nil,sizeof(nil)},&result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN && result.value.size == 3 && result.value.data[0] == 131 &&
              result.value.data[1] == 97 && result.value.data[2] == EXPECTED_ANSWER);
        beam_result_release(&result);
        CHECK(beam_vm_call(vm,"pkg_acceptance","orphan",(beam_bytes){arguments,argument_size},&result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN && result.value.size == 6 && result.value.data[0] == 131 &&
              result.value.data[1] == 98);
        DWORD pid = 0;
        for (unsigned i=2; i<6; ++i) pid = (pid << 8) | result.value.data[i];
        beam_result_release(&result);
        descendant = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); CHECK(descendant);
        CHECK(WaitForSingleObject(descendant,0) == WAIT_TIMEOUT);
        CHECK(beam_vm_terminate(&vm,NULL) == BEAM_OK);
        CHECK(WaitForSingleObject(descendant,5000) == WAIT_OBJECT_0);
        CHECK(CloseHandle(descendant)); descendant = NULL;
        CHECK(WaitForSingleObject(host_process.hProcess,0) == WAIT_TIMEOUT);
        CHECK(observe(&after));
        CHECK(restored(&before,&after));
    }
    status = 0; puts("WINDOWS_NATIVE_SDK_RESTORED");
done:
    beam_result_release(&result);
    if (beam_vm_terminate(&vm,NULL) != BEAM_OK) status = 1;
    if (beam_terminate(&system) != BEAM_OK) status = 1;
    if (descendant && !CloseHandle(descendant)) status = 1;
    if (host_process.hThread && !CloseHandle(host_process.hThread)) status = 1;
    if (host_process.hProcess) {
        if (!TerminateProcess(host_process.hProcess,0) ||
            WaitForSingleObject(host_process.hProcess,5000) != WAIT_OBJECT_0) status = 1;
        if (!CloseHandle(host_process.hProcess)) status = 1;
    }
    if (host_thread) {
        DWORD code = 1;
        if (!SetEvent(control.stop) || WaitForSingleObject(host_thread,5000) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(host_thread,&code) || code) status = 1;
        if (!CloseHandle(host_thread)) status = 1;
    }
    if (control.ready && !CloseHandle(control.ready)) status = 1;
    if (control.stop && !CloseHandle(control.stop)) status = 1;
    beam_composition_release(&composition);
    beam_composition_release(&failing);
    for (unsigned i=1; i<5; ++i) { free((void *)modules[i].origin); free((void *)modules[i].bytes.data); }
    free(bytes); free(arguments); free(python); free(origin); free(native); return status;
}
