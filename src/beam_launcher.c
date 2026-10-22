/* SPDX-License-Identifier: Apache-2.0 */
/* Generic process launcher. It deliberately has no VM or application policy. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#define LIMIT 32768

static int failure(const char *message)
{
    fprintf(stderr, "beam-launch: %s\n", message);
    return 1;
}

/* Four UTF-8 lines: version, runtime binding, runtime path, descriptor path.
 * Relative paths are interpreted from the launcher's directory, never cwd/PATH. */
static int config(FILE *f, char lines[4][LIMIT])
{
    for (int i=0;i<4;++i) {
        if (!fgets(lines[i],LIMIT,f)) return 0;
        size_t n=strlen(lines[i]);
        if (!n || lines[i][n-1]!='\n') return 0;
        lines[i][--n]=0;
        if (!n || strchr(lines[i],'\r')) return 0;
    }
    return fgetc(f)==EOF && !ferror(f) && !strcmp(lines[0],"beam-launcher-v1") &&
        (!strcmp(lines[1],"relative") || !strcmp(lines[1],"absolute"));
}

#ifdef _WIN32
static wchar_t *wide(const char *text)
{
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
    wchar_t *p=n ? malloc((size_t)n*sizeof(*p)) : NULL;
    if(p && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,p,n)) { free(p); p=NULL; }
    return p;
}
static int absolute(const wchar_t *s)
{
    return (s[0] && s[1]==L':' && (s[2]==L'\\' || s[2]==L'/')) ||
        (s[0]==L'\\' && s[1]==L'\\');
}
/* Quote the C runtime argv convention, including trailing backslashes and "". */
static int quote(wchar_t *out, size_t *used, const wchar_t *s)
{
    size_t need=wcslen(s)*2+4;
    if (need>LIMIT-*used) return 0;
    out[(*used)++]=L'"';
    while(*s) {
        size_t slashes=0;
        while(*s==L'\\') { ++slashes; ++s; }
        size_t copies=(*s==L'"' || !*s) ? slashes*2 : slashes;
        while(copies--) out[(*used)++]=L'\\';
        if(*s==L'"') out[(*used)++]=L'\\';
        if(*s) out[(*used)++]=*s++;
    }
    out[(*used)++]=L'"'; out[(*used)++]=L' '; out[*used]=0;
    return 1;
}
int wmain(int argc, wchar_t **argv)
{
    wchar_t self[LIMIT], sidecar[LIMIT], runtime[LIMIT], descriptor[LIMIT], command[LIMIT]={0};
    char lines[4][LIMIT]; size_t used=0;
    DWORD n=GetModuleFileNameW(NULL,self,LIMIT);
    if(!n || n>=LIMIT-16) return failure("cannot locate launcher");
    swprintf(sidecar,LIMIT,L"%ls.beam-launcher",self);
    FILE *f=_wfopen(sidecar,L"rb");
    if(!f) return failure("launcher configuration is unavailable");
    int valid=config(f,lines); if(fclose(f)) valid=0;
    if(!valid) return failure("invalid launcher configuration");
    wchar_t *r=wide(lines[2]), *d=wide(lines[3]), *end=wcsrchr(self,L'\\');
    if(!r || !d || !end || absolute(d) || d[0]==L'/' || d[0]==L'\\' ||
        (!strcmp(lines[1],"absolute") != absolute(r))) { free(r);free(d);return failure("invalid runtime or descriptor binding"); }
    *end=0;
    if(wcslen(self)+wcslen(r)+2>=LIMIT || wcslen(self)+wcslen(d)+2>=LIMIT) {
        free(r);free(d);return failure("launcher path is too long");
    }
    if(absolute(r)) wcscpy(runtime,r); else swprintf(runtime,LIMIT,L"%ls\\%ls",self,r);
    swprintf(descriptor,LIMIT,L"%ls\\%ls",self,d); free(r);free(d);
    wchar_t *fixed[]={runtime,L"run",L"--application",descriptor,L"--"};
    for(size_t i=0;i<5;++i) if(!quote(command,&used,fixed[i])) return failure("command is too long");
    for(int i=1;i<argc;++i) if(!quote(command,&used,argv[i])) return failure("command is too long");
    STARTUPINFOW startup={.cb=sizeof(startup)}; PROCESS_INFORMATION process={0};
    if(!CreateProcessW(runtime,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process))
        return failure("selected runtime cannot be executed");
    CloseHandle(process.hThread);
    /* The child shares console and streams. Ignore our copy of console events
     * after creation so it owns interrupt handling and determines the status. */
    SetConsoleCtrlHandler(NULL,TRUE);
    DWORD code=1;
    if(WaitForSingleObject(process.hProcess,INFINITE)==WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess); return (int)code;
}
#else
int main(int argc, char **argv)
{
    char self[LIMIT], sidecar[LIMIT], runtime[LIMIT], descriptor[LIMIT], lines[4][LIMIT];
#ifdef __APPLE__
    uint32_t capacity=sizeof(self);
    if(_NSGetExecutablePath(self,&capacity)) return failure("cannot locate launcher");
    char *resolved=realpath(self,NULL);
    if(!resolved || strlen(resolved)>=sizeof(self)) { free(resolved); return failure("cannot resolve launcher"); }
    strcpy(self,resolved);free(resolved);
#else
    ssize_t n=readlink("/proc/self/exe",self,sizeof(self)-1);
    if(n<=0 || n>=sizeof(self)-1) return failure("cannot locate launcher");
    self[n]=0;
#endif
    if(snprintf(sidecar,sizeof(sidecar),"%s.beam-launcher",self)>=sizeof(sidecar)) return failure("launcher path is too long");
    FILE *f=fopen(sidecar,"rb");
    if(!f) return failure("launcher configuration is unavailable");
    int valid=config(f,lines); if(fclose(f)) valid=0;
    if(!valid) return failure("invalid launcher configuration");
    char *end=strrchr(self,'/'); if(!end) return failure("launcher directory is unavailable"); *end=0;
    if(lines[3][0]=='/' || (!strcmp(lines[1],"absolute") != (lines[2][0]=='/')))
        return failure("invalid runtime or descriptor binding");
    int length=!strcmp(lines[1],"absolute") ? snprintf(runtime,sizeof(runtime),"%s",lines[2]) :
        snprintf(runtime,sizeof(runtime),"%s/%s",self,lines[2]);
    if(length<0 || length>=sizeof(runtime) ||
        snprintf(descriptor,sizeof(descriptor),"%s/%s",self,lines[3])>=sizeof(descriptor)) return failure("launcher path is too long");
    char **args=calloc((size_t)argc+6,sizeof(*args));
    if(!args) return failure("out of memory");
    args[0]=runtime;args[1]="run";args[2]="--application";args[3]=descriptor;args[4]="--";
    for(int i=1;i<argc;++i) args[i+4]=argv[i];
    execv(runtime,args);
    free(args); return failure("selected runtime cannot be executed");
}
#endif
