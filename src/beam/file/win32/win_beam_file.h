/* SPDX-License-Identifier: Apache-2.0 */
/* Modern UTF-8 facade over the retained Windows file resource implementation. */
int beam_efile_contract_open(beam_file_operation *op, const efile_path_t *path,
    enum efile_modes_t modes, ErlNifResourceType *type, efile_data_t **out)
{
    if (beam_file_test_before("open")) return EIO;
    if (beam_file_operation_cancelled(op)) return ETIMEDOUT;
    int error = beam_efile_open(path, modes, type, out);
    if (!error && beam_file_operation_cancelled(op)) {
        posix_errno_t ignored;
        erts_atomic32_set_relb(&(*out)->state, EFILE_STATE_CLOSED);
        beam_efile_close(*out, &ignored);
        *out = NULL;
        return ETIMEDOUT;
    }
    return error;
}

Sint64 beam_efile_contract_read(beam_file_operation *op, efile_data_t *d,
    unsigned char *bytes, size_t size)
{
    DWORD count = 0;
    if (beam_file_test_before("read")) { d->posix_errno = EIO; return -1; }
    if (beam_file_operation_cancelled(op)) { d->posix_errno = ETIMEDOUT; return -1; }
    BOOL ok = ReadFile(((efile_win_t *)d)->handle, bytes, (DWORD)MIN(size, MAXDWORD), &count, NULL);
    DWORD error = GetLastError();
    if (!ok && error != ERROR_BROKEN_PIPE && error != ERROR_HANDLE_EOF) {
        d->posix_errno = windows_to_posix_errno(error); return -1;
    }
    op->progress += count;
    beam_file_test_after("read");
    return count;
}

int beam_efile_contract_write(beam_file_operation *op, efile_data_t *d,
    const unsigned char *bytes, size_t size)
{
    while (op->progress < size) {
        DWORD count = 0;
        if (beam_file_test_before("write")) return ENOSPC;
        if (beam_file_operation_cancelled(op)) return ETIMEDOUT;
        if (!WriteFile(((efile_win_t *)d)->handle, bytes + op->progress,
            (DWORD)MIN(size-op->progress, 65536), &count, NULL)) return windows_to_posix_errno(GetLastError());
        if (!count) return EIO;
        op->progress += count;
        beam_file_test_after("write");
    }
    return 0;
}

ERL_NIF_TERM beam_efile_contract_path(beam_file_operation *op,
    ERL_NIF_TERM action, const efile_path_t *path, const efile_path_t *target)
{
    ErlNifEnv *env = op->env;
    int error = 0; char name[32];
    ERL_NIF_TERM result = enif_make_atom(env, "ok");
    if (!enif_get_atom(env, action, name, sizeof(name), ERL_NIF_LATIN1)) return enif_make_badarg(env);
    if (beam_file_operation_cancelled(op)) error = ETIMEDOUT;
    else if (!strcmp(name, "info")) {
        efile_fileinfo_t info;
        error = beam_efile_read_info(path, 0, &info);
        if (!error) {
            ERL_NIF_TERM keys[] = {enif_make_atom(env,"size"), enif_make_atom(env,"type")};
            const char *kind = info.type == EFILE_FILETYPE_REGULAR ? "regular" :
                info.type == EFILE_FILETYPE_DIRECTORY ? "directory" :
                info.type == EFILE_FILETYPE_SYMLINK ? "symlink" :
                info.type == EFILE_FILETYPE_DEVICE ? "device" : "other";
            ERL_NIF_TERM values[] = {enif_make_int64(env, info.size), enif_make_atom(env,kind)};
            if (!enif_make_map_from_arrays(env,keys,values,2,&result)) return enif_make_badarg(env);
            result = enif_make_tuple2(env,enif_make_atom(env,"ok"),result);
        }
    } else if (!strcmp(name,"list_directory")) {
        const wchar_t *base = (const wchar_t *)path->data;
        size_t length = wcslen(base);
        wchar_t *pattern = enif_alloc((length+3)*sizeof(*pattern));
        if (!pattern) error = ENOMEM;
        else {
            wcscpy(pattern,base); wcscpy(pattern+length,L"\\*");
            WIN32_FIND_DATAW data;
            HANDLE find = FindFirstFileW(pattern,&data);
            enif_free(pattern);
            ERL_NIF_TERM names = enif_make_list(env,0);
            if (find == INVALID_HANDLE_VALUE) error = windows_to_posix_errno(GetLastError());
            else {
                for (;;) {
                    if (beam_file_operation_cancelled(op)) { error = ETIMEDOUT; break; }
                    if (wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L"..")) {
                        int size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,NULL,0,NULL,NULL);
                        if (!size) { error = EILSEQ; break; }
                        char *utf8 = enif_alloc(size);
                        if (!utf8) { error = ENOMEM; break; }
                        int converted = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,utf8,size,NULL,NULL);
                        ERL_NIF_TERM binary;
                        unsigned char *bytes = converted ? enif_make_new_binary(env,(size_t)size-1,&binary) : NULL;
                        if (bytes) { memcpy(bytes,utf8,(size_t)size-1); names = enif_make_list_cell(env,binary,names); }
                        enif_free(utf8);
                        if (!bytes) { error = converted ? ENOMEM : EILSEQ; break; }
                    }
                    if (!FindNextFileW(find,&data)) {
                        DWORD failure = GetLastError();
                        if (failure != ERROR_NO_MORE_FILES) error = windows_to_posix_errno(failure);
                        break;
                    }
                }
                if (!FindClose(find) && !error) error = windows_to_posix_errno(GetLastError());
            }
            if (!error) result = enif_make_tuple2(env,enif_make_atom(env,"ok"),names);
        }
    } else if (!strcmp(name,"make_directory")) error = beam_efile_make_dir(path);
    else if (!strcmp(name,"remove_file")) error = beam_efile_del_file(path);
    else if (!strcmp(name,"remove_directory")) error = beam_efile_del_dir(path);
    else if (!strcmp(name,"rename") && target) error = beam_efile_rename(path,target);
    else return enif_make_badarg(env);
    if (error) result = enif_make_tuple2(env,enif_make_atom(env,"error"),enif_make_atom(env,erl_errno_id(error)));
    return result;
}
