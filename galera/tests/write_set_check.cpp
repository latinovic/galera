/*
 * Copyright (C) 2010-2012 Codership Oy <info@codership.com>
 */

#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "gu_logger.hpp"
#include "certification.hpp"
#include "wsdb.cpp"
#include "gcs_action_source.hpp"
#include <cstdlib>

#include <check.h>

using namespace std;
using namespace galera;

typedef std::vector<galera::KeyPart> KeyPartSequence;


START_TEST(test_key1)
{
    static char k1[16];
    static char k2[256];
    static char k3[1 << 21];
    static char k4[(1 << 22) - 5];

    memset(k1, 0xab, sizeof(k1));
    memset(k2, 0xcd, sizeof(k2));
    memset(k3, 0x9e, sizeof(k3));
    memset(k4, 0x8f, sizeof(k4));

    const wsrep_key_part_t kiovec[4] = {
        {k1, sizeof k1 },
        {k2, sizeof k2 },
        {k3, sizeof k3 },
        {k4, sizeof k4 }
    };

    Key key(1, kiovec, 4, 0);
    size_t expected_size(0);

#ifndef GALERA_KEY_VLQ
    expected_size += 1 + std::min(sizeof k1, size_t(0xff));
    expected_size += 1 + std::min(sizeof k2, size_t(0xff));
    expected_size += 1 + std::min(sizeof k3, size_t(0xff));
    expected_size += 1 + std::min(sizeof k4, size_t(0xff));
    expected_size += sizeof(uint16_t);
#else
    expected_size += gu::uleb128_size(sizeof k1) + sizeof k1;
    expected_size += gu::uleb128_size(sizeof k2) + sizeof k2;
    expected_size += gu::uleb128_size(sizeof k3) + sizeof k3;
    expected_size += gu::uleb128_size(sizeof k4) + sizeof k4;
    expected_size += gu::uleb128_size(expected_size);
#endif

    fail_unless(serial_size(key) == expected_size, "%ld <-> %ld",
                serial_size(key), expected_size);

    KeyPartSequence kp(key.key_parts<KeyPartSequence>());
    fail_unless(kp.size() == 4);

    gu::Buffer buf(galera::serial_size(key));
    serialize(key, &buf[0], buf.size(), 0);
    Key key2(1);
    unserialize(&buf[0], buf.size(), 0, key2);
    fail_unless(key2 == key);
}
END_TEST


START_TEST(test_key2)
{
    static char k1[16];
    static char k2[256];
    static char k3[1 << 21];
    static char k4[(1 << 22) - 5];

    memset(k1, 0xab, sizeof(k1));
    memset(k2, 0xcd, sizeof(k2));
    memset(k3, 0x9e, sizeof(k3));
    memset(k4, 0x8f, sizeof(k4));

    const wsrep_key_part_t kiovec[4] = {
        {k1, sizeof k1 },
        {k2, sizeof k2 },
        {k3, sizeof k3 },
        {k4, sizeof k4 }
    };

    Key key(2, kiovec, 4, 0);
    size_t expected_size(0);

    expected_size += 1; // flags
#ifndef GALERA_KEY_VLQ
    expected_size += 1 + std::min(sizeof k1, size_t(0xff));
    expected_size += 1 + std::min(sizeof k2, size_t(0xff));
    expected_size += 1 + std::min(sizeof k3, size_t(0xff));
    expected_size += 1 + std::min(sizeof k4, size_t(0xff));
    expected_size += sizeof(uint16_t);
#else
    expected_size += gu::uleb128_size(sizeof k1) + sizeof k1;
    expected_size += gu::uleb128_size(sizeof k2) + sizeof k2;
    expected_size += gu::uleb128_size(sizeof k3) + sizeof k3;
    expected_size += gu::uleb128_size(sizeof k4) + sizeof k4;
    expected_size += gu::uleb128_size(expected_size);
#endif

    fail_unless(serial_size(key) == expected_size, "%ld <-> %ld",
                serial_size(key), expected_size);

    KeyPartSequence kp(key.key_parts<KeyPartSequence>());
    fail_unless(kp.size() == 4);

    gu::Buffer buf(serial_size(key));
    serialize(key, &buf[0], buf.size(), 0);
    Key key2(2);
    unserialize(&buf[0], buf.size(), 0, key2);
    fail_unless(key2 == key);
}
END_TEST

START_TEST(test_write_set1)
{
    WriteSet ws(1);

    const wsrep_key_part_t key1[2] = {
        {void_cast("dbt\0t1"), 6},
        {void_cast("aaa")    , 3}
    };

    const wsrep_key_part_t key2[2] = {
        {void_cast("dbt\0t2"), 6},
        {void_cast("bbbb"), 4}
    };

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << serial_size(ws);
    ws.append_key(Key(1, key1, 2, 0));
    log_info << "ws1 " << serial_size(ws);
    ws.append_key(Key(1, key2, 2, 0));
    log_info << "ws2 " << serial_size(ws);

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << gu::serial_size4(rbrbuf);
    log_info << "wsrbr " << serial_size(ws);

    gu::Buffer buf(serial_size(ws));

    serialize(ws, &buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
#ifndef GALERA_KEY_VLQ
        + 2 + 1 + 6 + 1 + 3 // key1
        + 2 + 1 + 6 + 1 + 4 // key2
#else
        + 1 + 1 + 6 + 1 + 3 // key1
        + 1 + 1 + 6 + 1 + 4 // key2
#endif
        + 4 + 6; // rbr
    fail_unless(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                buf.size(), expected_size, serial_size(ws));


    WriteSet ws2(0);

    size_t ret = unserialize(&buf[0], buf.size(), 0, ws2);
    fail_unless(ret == expected_size);

    WriteSet::KeySequence rks;
    ws.get_keys(rks);

    WriteSet::KeySequence rks2;
    ws.get_keys(rks2);

    fail_unless(rks2 == rks);

    fail_unless(ws2.get_data() == ws.get_data());

}
END_TEST



START_TEST(test_write_set2)
{
    WriteSet ws(2);

    const wsrep_key_part_t key1[2] = {
        {void_cast("dbt\0t1"), 6},
        {void_cast("aaa")    , 3}
    };

    const wsrep_key_part_t key2[2] = {
        {void_cast("dbt\0t2"), 6},
        {void_cast("bbbb"), 4}
    };

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << serial_size(ws);
    ws.append_key(Key(2, key1, 2, 0));
    log_info << "ws1 " << serial_size(ws);
    ws.append_key(Key(2, key2, 2, 0));
    log_info << "ws2 " << serial_size(ws);

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << gu::serial_size4(rbrbuf);
    log_info << "wsrbr " << serial_size(ws);

    gu::Buffer buf(serial_size(ws));

    serialize(ws, &buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
#ifndef GALERA_KEY_VLQ
        + 2 + 1 + 1 + 6 + 1 + 3 // key1
        + 2 + 1 + 1 + 6 + 1 + 4 // key2
#else
        + 1 + 1 + 6 + 1 + 3 // key1
        + 1 + 1 + 6 + 1 + 4 // key2
#endif
        + 4 + 6; // rbr
    fail_unless(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                buf.size(), expected_size, serial_size(ws));


    WriteSet ws2(2);

    size_t ret = unserialize(&buf[0], buf.size(), 0, ws2);
    fail_unless(ret == expected_size);

    WriteSet::KeySequence rks;
    ws.get_keys(rks);

    WriteSet::KeySequence rks2;
    ws2.get_keys(rks2);

    fail_unless(rks2 == rks);

    fail_unless(ws2.get_data() == ws.get_data());

}
END_TEST



START_TEST(test_mapped_buffer)
{
    string wd("/tmp");
    MappedBuffer mb(wd, 1 << 8);

    mb.resize(16);
    for (size_t i = 0; i < 16; ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

    mb.resize(1 << 8);
    for (size_t i = 0; i < 16; ++i)
    {
        fail_unless(mb[i] == static_cast<gu::byte_t>(i));
    }

    for (size_t i = 16; i < (1 << 8); ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

    mb.resize(1 << 20);

    for (size_t i = 0; i < (1 << 8); ++i)
    {
        fail_unless(mb[i] == static_cast<gu::byte_t>(i));
    }

    for (size_t i = 0; i < (1 << 20); ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

}
END_TEST


START_TEST(test_cert_hierarchical_v1)
{
    log_info << "test_cert_hierarchical_v1";
    struct wsinfo_ {
        wsrep_uuid_t     uuid;
        wsrep_conn_id_t  conn_id;
        wsrep_trx_id_t   trx_id;
        wsrep_key_part_t key[3];
        size_t           iov_len;
        wsrep_seqno_t    local_seqno;
        wsrep_seqno_t    global_seqno;
        wsrep_seqno_t    last_seen_seqno;
        wsrep_seqno_t    expected_depends_seqno;
        int              flags;
        Certification::TestResult result;
    } wsi[] = {
        // 1 - 3, test symmetric case for dependencies
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          1, 1, 0, 0, 0, Certification::TEST_OK},
        // 2: depends on 1, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          2, 2, 0, 1, 0, Certification::TEST_OK},
        // 3: depends on 2, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          3, 3, 0, 2, 0, Certification::TEST_OK},
        // 4 - 8, test symmetric case for conflicts
        // 4: depends on 3, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          4, 4, 3, 3, 0, Certification::TEST_OK},
        // 5: conflict with 4
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          5, 5, 3, -1, 0, Certification::TEST_FAILED},
        // 6: depends on 4 (failed 5 not present in index), no conflict
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          6, 6, 5, 4, 0, Certification::TEST_OK},
        // 7: conflicts with 6
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          7, 7, 5, -1, 0, Certification::TEST_FAILED},
        // 8: to isolation: must not conflict, depends on global_seqno - 1
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          8, 8, 5, 7, TrxHandle::F_ISOLATION, Certification::TEST_OK},
        // 9: to isolation: must not conflict, depends on global_seqno - 1
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          9, 9, 5, 8, TrxHandle::F_ISOLATION, Certification::TEST_OK},


    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    gu::Config conf;
    galera::Certification cert(conf);
    cert.assign_initial_position(0, 1);

    mark_point();

    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(new TrxHandle(1, wsi[i].uuid, wsi[i].conn_id,
                                     wsi[i].trx_id, false));
        trx->append_key(Key(1, wsi[i].key, wsi[i].iov_len, 0));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = new TrxHandle();
        size_t offset(unserialize(&buf[0], buf.size(), 0, *trx));
        log_info << "ws[" << i << "]: " << buf.size() - offset;
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == wsi[i].result, "g: %lld r: %d er: %d",
                    trx->global_seqno(), result, wsi[i].result);
        fail_unless(trx->depends_seqno() == wsi[i].expected_depends_seqno,
                    "wsi: %zu g: %lld ld: %lld eld: %lld",
                    i, trx->global_seqno(), trx->depends_seqno(),
                    wsi[i].expected_depends_seqno);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST


START_TEST(test_cert_hierarchical_v2)
{
    log_info << "test_cert_hierarchical_v2";
    const int version(2);
    struct wsinfo_ {
        wsrep_uuid_t     uuid;
        wsrep_conn_id_t  conn_id;
        wsrep_trx_id_t   trx_id;
        wsrep_key_part_t key[3];
        size_t           iov_len;
        bool             shared;
        wsrep_seqno_t    local_seqno;
        wsrep_seqno_t    global_seqno;
        wsrep_seqno_t    last_seen_seqno;
        wsrep_seqno_t    expected_depends_seqno;
        int              flags;
        Certification::TestResult result;
    } wsi[] = {
        // 1 - 4: shared - shared
        // First four cases are shared keys, they should not collide or
        // generate dependency
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1, true,
          1, 1, 0, 0, 0, Certification::TEST_OK},
        // 2: no dependencies
        { { {1, } }, 1, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          2, 2, 0, 0, 0, Certification::TEST_OK},
        // 3: no dependencies
        { { {2, } }, 1, 3,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          3, 3, 0, 0, 0, Certification::TEST_OK},
        // 4: no dependencies
        { { {3, } }, 1, 4,
          { {void_cast("1"), 1}, }, 1, true,
          4, 4, 0, 0, 0, Certification::TEST_OK},
        // 5: shared - exclusive
        // 5: depends on 4
        { { {2, } }, 1, 5,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          5, 5, 0, 4, 0, Certification::TEST_OK},
        // 6 - 8: exclusive - shared
        // 6: collides with 5
        { { {1, } }, 1, 6,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          6, 6, 4, -1, 0, Certification::TEST_FAILED},
        // 7: collides with 5
        { { {1, } }, 1, 7,
          { {void_cast("1"), 1}, }, 1, true,
          7, 7, 4, -1, 0, Certification::TEST_FAILED},
        // 8: collides with 5
        { { {1, } }, 1, 8,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, true,
          8, 8, 4, -1, 0, Certification::TEST_FAILED},
        // 9 - 10: shared key shadows dependency to 5
        // 9: depends on 5
        { { {2, } }, 1, 9,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          9, 9, 0, 5, 0, Certification::TEST_OK},
        // 10: depends on 5
        { { {2, } }, 1, 10,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          10, 10, 6, 5, 0, Certification::TEST_OK},
        // 11 - 13: exclusive - shared - exclusive dependency
        { { {2, } }, 1, 11,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          11, 11, 10, 10, 0, Certification::TEST_OK},
        { { {2, } }, 1, 12,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          12, 12, 10, 11, 0, Certification::TEST_OK},
        { { {2, } }, 1, 13,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          13, 13, 10, 12, 0, Certification::TEST_OK},

    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    gu::Config conf;
    galera::Certification cert(conf);
    cert.assign_initial_position(0, version);

    mark_point();

    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(new TrxHandle(version, wsi[i].uuid, wsi[i].conn_id,
                                     wsi[i].trx_id, false));
        trx->append_key(Key(version, wsi[i].key, wsi[i].iov_len,
                            (wsi[i].shared == true ? Key::F_SHARED : 0)));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = new TrxHandle();
        size_t offset(unserialize(&buf[0], buf.size(), 0, *trx));
        log_info << "ws[" << i << "]: " << buf.size() - offset;
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == wsi[i].result, "g: %lld res: %d exp: %d",
                    trx->global_seqno(), result, wsi[i].result);
        fail_unless(trx->depends_seqno() == wsi[i].expected_depends_seqno,
                    "wsi: %zu g: %lld ld: %lld eld: %lld",
                    i, trx->global_seqno(), trx->depends_seqno(),
                    wsi[i].expected_depends_seqno);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST


START_TEST(test_trac_726)
{
    log_info << "test_trac_726";
    const int version(2);
    gu::Config conf;
    galera::Certification cert(conf);
    wsrep_uuid_t uuid1 = {{1, }};
    wsrep_uuid_t uuid2 = {{2, }};
    cert.assign_initial_position(0, version);

    mark_point();

    wsrep_key_part_t key1 = {void_cast("1"), 1};
    wsrep_key_part_t key2 = {void_cast("2"), 1};

    {
        TrxHandle* trx(new TrxHandle(version, uuid1, 0, 0, false));

        trx->append_key(Key(version, &key1, 1, 0));
        trx->set_last_seen_seqno(0);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = new TrxHandle();
        size_t offset(unserialize(&buf[0], buf.size(), 0, *trx));
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, 1, 1);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == Certification::TEST_OK);
        cert.set_trx_committed(trx);
        trx->unref();
    }

    {
        TrxHandle* trx(new TrxHandle(version, uuid2, 0, 0, false));

        trx->append_key(Key(version, &key2, 1, 0));
        trx->append_key(Key(version, &key2, 1, Key::F_SHARED));
        trx->append_key(Key(version, &key1, 1, 0));

        trx->set_last_seen_seqno(0);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = new TrxHandle();
        size_t offset(unserialize(&buf[0], buf.size(), 0, *trx));
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, 2, 2);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == Certification::TEST_FAILED);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST

Suite* write_set_suite()
{
    Suite* s = suite_create("write_set");
    TCase* tc;

    tc = tcase_create("test_key1");
    tcase_add_test(tc, test_key1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_key2");
    tcase_add_test(tc, test_key2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_set1");
    tcase_add_test(tc, test_write_set1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_set2");
    tcase_add_test(tc, test_write_set2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_mapped_buffer");
    tcase_add_test(tc, test_mapped_buffer);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v1");
    tcase_add_test(tc, test_cert_hierarchical_v1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v2");
    tcase_add_test(tc, test_cert_hierarchical_v2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_726");
    tcase_add_test(tc, test_trac_726);
    suite_add_tcase(s, tc);

    return s;
}
