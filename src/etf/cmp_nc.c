/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2020-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 *
 */
#include "etf_internal.h"
#include "putget.h"


#include <string.h>
/*
 * Comparison of node container types (pid, port, ref). Comparison
 * done the same way as ERTS compare them. In ref and port case,
 * node info is compared before other data, and in pid case node
 * info is compared after other data.
 */

static int cmp_nodes(char *aname, unsigned int acreation,
                     char *bname, unsigned int bcreation)
{
    int differ = strcmp(aname, bname);
    if (differ)
        return differ;
    if (acreation == bcreation)
        return 0;
    if (acreation < bcreation)
        return -1;
    return 1;
}

int
beam_etf_cmp_refs(BeamEtfRef *a, BeamEtfRef *b)
{
    int differ;
    int i, alen, blen;
    unsigned int *anum, *bnum;

    differ = cmp_nodes(a->node, a->creation, b->node, b->creation);
    if (differ)
        return differ;

    alen = a->len;
    blen = b->len;

    anum = &a->n[0];
    bnum = &b->n[0];

    if (alen != blen) {
        if (alen > blen) {
            do {
                if (anum[alen - 1] != 0)
                    return 1;
                alen--;
            } while (alen > blen);
        }
        else {
            do {
                if (bnum[blen - 1] != 0)
                    return -1;
                blen--;
            } while (alen < blen);
        }
    }

    for (i = alen - 1; i >= 0; i--)
        if (anum[i] != bnum[i])
            return anum[i] < bnum[i] ? -1 : 1;

    return 0;
}

int
beam_etf_cmp_pids(BeamEtfPid *a, BeamEtfPid *b)
{
    int differ;

    if (a->serial != b->serial)
        return a->serial < b->serial ? -1 : 1;

    if (a->num != b->num)
        return a->num < b->num ? -1 : 1;

    differ = cmp_nodes(a->node, a->creation, b->node, b->creation);
    if (differ)
        return differ;

    return 0;
}

int
beam_etf_cmp_ports(BeamEtfPort *a, BeamEtfPort *b)
{
    int differ;

    differ = cmp_nodes(a->node, a->creation, b->node, b->creation);
    if (differ)
        return differ;

    if (a->id != b->id)
        return a->id < b->id ? -1 : 1;

    return 0;
}
