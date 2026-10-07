/*
 * Userspace check that the SID cache talks to LMDB.
 * Linked against libpvfs2. No server required.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvfs2-internal.h"
#include "sid.h"
#include "policyeval.h"
#include "quicklist.h"

static int failures = 0;

static void expect(const char *what, int cond)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL %s\n", what);
        failures++;
    }
    else
    {
        printf("ok %s\n", what);
    }
}

static void fill_attrs(int *attrs, int meta, int tier, int space)
{
    int i;

    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        attrs[i] = -1;
    }
    attrs[SID_attr_meta] = meta;
    attrs[SID_attr_tier] = tier;
    attrs[SID_attr_space] = space;
    /* The metadata policy spreads on rack. A positive rack lets
     * SID_select mark the server so a wrapped join does not add it twice.
     */
    attrs[SID_attr_rack] = 1;
}

static int list_has(SID_server_list_t *list, const PVFS_SID *sid, int *n)
{
    struct qlist_head *pos;
    int found = 0;

    *n = 0;
    qlist_for_each(pos, &list->link)
    {
        SID_server_list_t *one = qlist_entry(pos, SID_server_list_t, link);
        (*n)++;
        if (PVFS_SID_EQ(&one->server_sid, sid))
        {
            found = 1;
        }
    }
    return found;
}

int main(void)
{
    PVFS_SID good;
    PVFS_SID bad;
    PVFS_BMI_addr_t addr = 0;
    int good_attr[SID_NUM_ATTR];
    int bad_attr[SID_NUM_ATTR];
    int count = -1;
    int copies = -1;
    int n = 0;
    int rc;
    SID_server_list_t selected;
    SID_server_list_t again;
    struct SID_type_s typ;
    const char *good_url = "tcp://good:3334";
    const char *bad_url = "tcp://bad:3334";
    const PVFS_fs_id fsid = 9;

    rc = PVFS_SID_str2bin("11111111-1111-4111-8111-111111111111", &good);
    expect("parse good sid", rc == 0);
    rc = PVFS_SID_str2bin("22222222-2222-4222-8222-222222222222", &bad);
    expect("parse bad sid", rc == 0);

    fill_attrs(good_attr, SID_attr_meta_Y, SID_attr_tier_primary, 80);
    fill_attrs(bad_attr, SID_attr_meta_N, SID_attr_tier_primary, 80);

    rc = SID_initialize();
    expect("SID_initialize", rc == 0);

    rc = SID_add(&good, 1001, good_url, good_attr);
    expect("SID_add good", rc == 0);
    rc = SID_add(&good, 1001, good_url, good_attr);
    expect("second SID_add of the same SID", rc == 0);
    rc = SID_add(&bad, 1002, bad_url, bad_attr);
    expect("SID_add bad", rc == 0);

    typ.fsid = fsid;
    typ.server_type = SID_SERVER_META | SID_SERVER_DATA;
    rc = SID_update_type(&good, &typ);
    expect("type good meta|data", rc == 0);
    typ.server_type = SID_SERVER_DATA;
    rc = SID_update_type(&bad, &typ);
    expect("type bad data", rc == 0);

    rc = PVFS_SID_get_addr(&addr, &good);
    expect("get_addr", rc == 0 && addr == 1001);

    rc = PVFS_SID_count_meta(fsid, &count);
    expect("count meta", rc == 0 && count == 1);
    rc = PVFS_SID_count_io(fsid, &count);
    expect("count data", rc == 0 && count == 2);

    INIT_QLIST_HEAD(&selected.link);
    rc = SID_select_servers(&SID_policies[0], 1, &copies, &selected);
    expect("select metadata policy", rc == 0);
    expect("select returned the meta server",
           list_has(&selected, &good, &n) == 1);
    expect("select excluded the other server",
           list_has(&selected, &bad, &n) == 0);
    expect("copies matches the list", copies == n && n == 1);

    rc = SID_delete(&good);
    expect("SID_delete", rc == 0);
    {
        SID_cacheval_t *gone = NULL;
        rc = SID_cache_get(SID_db, &good, &gone);
        expect("deleted sid is gone", rc == -PVFS_ENOENT && gone == NULL);
        SID_cacheval_free(&gone);
    }
    rc = PVFS_SID_count_meta(fsid, &count);
    expect("meta count after delete", rc == 0 && count == 0);
    rc = PVFS_SID_count_io(fsid, &count);
    expect("data count after delete", rc == 0 && count == 1);

    rc = SID_add(&good, 1001, good_url, good_attr);
    expect("SID_add after delete", rc == 0);
    INIT_QLIST_HEAD(&again.link);
    copies = -1;
    rc = SID_select_servers(&SID_policies[0], 1, &copies, &again);
    expect("select after re-add", rc == 0);
    expect("re-add still matches the policy",
           list_has(&again, &good, &n) == 1 && copies == 1);

    rc = SID_finalize();
    expect("SID_finalize", rc == 0);

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("sidcache lmdb checks passed\n");
    return 0;
}
