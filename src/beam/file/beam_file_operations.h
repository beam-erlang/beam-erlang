/* SPDX-License-Identifier: Apache-2.0 */
/* Included by prim_file_nif.c: private BEAM facade operations. */
extern int erts_components_stopping(void);

extern void erts_components_set_native_cleanup(int native_code, const char *message);
static void beam_file_cleanup_close(efile_data_t *d)
{
    int error;
    beam_file_unaccount(d);
    if (!beam_efile_close(d, &error)) {
        char message[128];
        snprintf(message, sizeof(message), "file close failed: %s", erl_errno_id(error));
        erts_components_set_native_cleanup(error, message);
    }
}

static void beam_queue_close(efile_data_t *d)
{
    struct beam_pending_close *item = enif_alloc(sizeof(*item));
    if (!item) ERTS_INTERNAL_ERROR("could not retain file cleanup");
    item->file = d;
    enif_mutex_lock(beam_close_mutex);
    item->next = beam_closes;
    beam_closes = item;
    d->beam_pending = item;
    enif_mutex_unlock(beam_close_mutex);
}

static void beam_file_finish_close(ErlNifEnv *env, efile_data_t *d)
{
    if (enif_thread_type() == ERL_NIF_THR_DIRTY_IO_SCHEDULER) {
        beam_file_cleanup_close(d);
    } else {
        ERL_NIF_TERM ref, message;
        /* Legacy handle inspection/dyncalls can finish on normal schedulers.
         * Their owner-death cleanup must not enter a blocking close there. */
        beam_queue_close(d);
        ref = enif_make_resource(env, d);
        message = enif_make_tuple2(env, am_close, ref);
        (void)enif_send(env, &erts_prim_file_pid, NULL, message);
    }
}

static int beam_take_close(efile_data_t *d)
{
    struct beam_pending_close **link, *item = NULL;
    enif_mutex_lock(beam_close_mutex);
    for (link = &beam_closes; *link; link = &(*link)->next) {
        if ((*link)->file == d) {
            item = *link;
            *link = item->next;
            d->beam_pending = NULL;
            break;
        }
    }
    enif_mutex_unlock(beam_close_mutex);
    if (item) enif_free(item);
    return item != NULL;
}

static void beam_drain_closes(void)
{
    /* Runtime workers have retired before NIF code and mappings are released. */
    while (beam_closes) {
        efile_data_t *d = beam_closes->file;
        (void)beam_take_close(d);
        beam_file_cleanup_close(d);
    }
}

int beam_file_operation_cancelled(beam_file_operation *op)
{
    if (op->cancelled || erts_components_stopping() ||
        !enif_is_current_process_alive(op->env) ||
        (op->resource && erts_atomic32_read_acqb(&op->resource->state) == EFILE_STATE_CLOSE_PENDING) ||
        (op->finite && enif_monotonic_time(ERL_NIF_NSEC) >= op->deadline)) {
        op->cancelled = 1;
        return 1;
    }
    return 0;
}

static ERL_NIF_TERM beam_file_error(ErlNifEnv *env, int error)
{
    return posix_error_to_tuple(env, error);
}

static ERL_NIF_TERM beam_file_progress_error(ErlNifEnv *env, int error, Uint64 count)
{
    ERL_NIF_TERM reason = enif_make_atom(env, erl_errno_id(error));
    return enif_make_tuple3(env, am_error, reason, enif_make_uint64(env, count));
}

static ERL_NIF_TERM beam_handle_state_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    efile_data_t *d;
    ErlNifPid owner;
    if (!get_file_data(env, argv[0], &d)) return enif_make_badarg(env);
    enif_self(env, &owner);
    if (enif_compare_pids(&owner, &d->beam_owner))
        return enif_make_atom(env, "not_owner");
    return erts_atomic32_read_acqb(&d->state) == EFILE_STATE_IDLE ? am_ok :
        enif_make_atom(env, "closed");
}

static ERL_NIF_TERM beam_operation_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    beam_file_operation op = {.env = env};
    const ERL_NIF_TERM *args;
    int count, error = 0;
    efile_path_t path, target;
    efile_data_t *d;
    ERL_NIF_TERM action = argv[0], result;
    if (!enif_get_tuple(env, argv[1], &count, &args)) return enif_make_badarg(env);
    if (!enif_is_identical(argv[2], enif_make_atom(env, "infinity"))) {
        if (!enif_get_int64(env, argv[2], &op.deadline)) return enif_make_badarg(env);
        op.finite = 1;
    }
    if (enif_is_identical(action, enif_make_atom(env, "open"))) {
        enum efile_modes_t modes;
        if (count != 2 || !enif_is_atom(env, args[1])) return enif_make_badarg(env);
        if ((error = beam_efile_marshal_path(env, args[0], &path))) return beam_file_error(env, error);
        modes = enif_is_identical(args[1], am_write) ? EFILE_MODE_WRITE : EFILE_MODE_READ;
        if ((error = beam_efile_contract_open(&op, &path, modes, efile_resource_type, &d)))
            return beam_file_error(env, error);
        return create_ref_or_error_tuple(env, d);
    }
    if (enif_is_identical(action, am_read) || enif_is_identical(action, am_write) ||
        enif_is_identical(action, am_close)) {
        ErlNifPid owner;
        if (count < 1 || !get_file_data(env, args[0], &d)) return enif_make_badarg(env);
        enif_self(env, &owner);
        if (enif_compare_pids(&owner, &d->beam_owner))
            return beam_file_error(env, EALREADY);
        if (enif_is_identical(action, am_close)) {
            ERL_NIF_TERM av[] = {args[0]};
            if (erts_atomic32_read_acqb(&d->state) == EFILE_STATE_CLOSED) return am_ok;
            return close_nif(env, 1, av);
        }
        if (erts_atomic32_cmpxchg_acqb(&d->state, EFILE_STATE_BUSY, EFILE_STATE_IDLE) != EFILE_STATE_IDLE)
            return beam_file_error(env, EBADF);
        op.resource = d;
        if (enif_is_identical(action, am_read)) {
            ErlNifUInt64 maximum;
            ErlNifBinary bytes;
            Sint64 n;
            if (count != 2 || !enif_get_uint64(env, args[1], &maximum) || maximum > (size_t)-1)
                result = enif_make_badarg(env);
            else if (!enif_alloc_binary((size_t)MIN(maximum, 65536), &bytes))
                result = beam_file_error(env, ENOMEM);
            else {
                n = maximum == 0 ? 0 : beam_efile_contract_read(&op, d, bytes.data, bytes.size);
                if (n < 0) {
                    enif_release_binary(&bytes);
                    result = beam_file_error(env, d->posix_errno);
                } else if (n == 0 && maximum != 0) {
                    enif_release_binary(&bytes);
                    result = am_eof;
                } else {
                    if ((size_t)n != bytes.size && !enif_realloc_binary(&bytes, (size_t)n))
                        ERTS_INTERNAL_ERROR("could not shrink file read");
                    result = enif_make_tuple2(env, am_ok, enif_make_binary(env, &bytes));
                }
            }
        } else {
            ErlNifBinary bytes;
            if (count != 2 || !enif_inspect_binary(env, args[1], &bytes))
                result = enif_make_badarg(env);
            else {
                error = beam_efile_contract_write(&op, d, bytes.data, bytes.size);
                result = error ? beam_file_progress_error(env, error, op.progress) : am_ok;
            }
        }
        if (erts_atomic32_cmpxchg_relb(&d->state, EFILE_STATE_IDLE, EFILE_STATE_BUSY) == EFILE_STATE_CLOSE_PENDING) {
            erts_atomic32_set_acqb(&d->state, EFILE_STATE_CLOSED);
            beam_file_finish_close(env, d);
        }
        return result;
    }
    if (count < 1 || (error = beam_efile_marshal_path(env, args[0], &path)))
        return beam_file_error(env, count < 1 ? EINVAL : error);
    if (count == 2 && (error = beam_efile_marshal_path(env, args[1], &target)))
        return beam_file_error(env, error);
    return beam_efile_contract_path(&op, action, &path, count == 2 ? &target : NULL);
}
