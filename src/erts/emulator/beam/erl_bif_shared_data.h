/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ERL_BIF_SHARED_DATA_H
#define ERL_BIF_SHARED_DATA_H

/* VM-private kernels; public adapters retain their own argument registers. */
Eterm erts_atomic_array_new(Process *process, Eterm arg1, Eterm arg2);
Eterm erts_atomic_array_get(Process *process, Eterm arg1, Eterm arg2);
Eterm erts_atomic_array_put(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_atomic_array_add(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_atomic_array_add_get(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_atomic_array_exchange(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_atomic_array_compare_exchange(Process *process, Eterm arg1, Eterm arg2, Eterm arg3, Eterm arg4);
Eterm erts_atomic_array_info(Process *process, Eterm arg1);
Eterm erts_counter_array_new(Process *process, Eterm arg1);
Eterm erts_counter_array_get(Process *process, Eterm arg1, Eterm arg2);
Eterm erts_counter_array_put(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_counter_array_add(Process *process, Eterm arg1, Eterm arg2, Eterm arg3);
Eterm erts_counter_array_info(Process *process, Eterm arg1);
#endif
