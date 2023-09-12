/**
 * @file test_pg_hba.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief pg-hba SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2022 Deutsche Telekom AG.
 * Copyright (c) 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/pg-hba"
#include "srds_augeas.c"
#include "srdsa_init.c"
#include "srdsa_load.c"
#include "srdsa_store.c"
#include "srdsa_common.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "pg-hba"

static int
setup_f(void **state)
{
    return tsetup_glob(state, AUG_TEST_MODULE, &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<" AUG_TEST_MODULE " xmlns=\"aug:" AUG_TEST_MODULE "\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/" AUG_TEST_MODULE "</config-file>\n"
            "  <entries-list>\n"
            "    <_seq>1</_seq>\n"
            "    <type>local</type>\n"
            "    <case>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <method>\n"
            "        <value>ident</value>\n"
            "        <option-list>\n"
            "          <_id>1</_id>\n"
            "          <option>\n"
            "            <word>sameuser</word>\n"
            "          </option>\n"
            "        </option-list>\n"
            "      </method>\n"
            "    </case>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>2</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>127.0.0.1/32</address>\n"
            "      <method>\n"
            "        <value>md5</value>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>3</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>foo.example.com</address>\n"
            "      <method>\n"
            "        <value>md5</value>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>4</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>.example.com</address>\n"
            "      <method>\n"
            "        <value>md5</value>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>5</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>::1/128</address>\n"
            "      <method>\n"
            "        <value>md5</value>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>6</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>.dev.example.com</address>\n"
            "      <method>\n"
            "        <value>gss</value>\n"
            "        <option-list>\n"
            "          <_id>1</_id>\n"
            "          <option>\n"
            "            <word>include_realm</word>\n"
            "            <value>0</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "        <option-list>\n"
            "          <_id>2</_id>\n"
            "          <option>\n"
            "            <word>krb_realm</word>\n"
            "            <value>EXAMPLE.COM</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "        <option-list>\n"
            "          <_id>3</_id>\n"
            "          <option>\n"
            "            <word>map</word>\n"
            "            <value>somemap</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "  <entries-list>\n"
            "    <_seq>7</_seq>\n"
            "    <type>host</type>\n"
            "    <case2>\n"
            "      <database-list>\n"
            "        <_id>1</_id>\n"
            "        <database>all</database>\n"
            "      </database-list>\n"
            "      <user-list>\n"
            "        <_id>1</_id>\n"
            "        <user>all</user>\n"
            "      </user-list>\n"
            "      <address>.dev.example.com</address>\n"
            "      <method>\n"
            "        <value>ldap</value>\n"
            "        <option-list>\n"
            "          <_id>1</_id>\n"
            "          <option>\n"
            "            <word>ldapserver</word>\n"
            "            <value>auth.example.com</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "        <option-list>\n"
            "          <_id>2</_id>\n"
            "          <option>\n"
            "            <word>ldaptls</word>\n"
            "            <value>1</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "        <option-list>\n"
            "          <_id>3</_id>\n"
            "          <option>\n"
            "            <word>ldapprefix</word>\n"
            "            <value>uid=</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "        <option-list>\n"
            "          <_id>4</_id>\n"
            "          <option>\n"
            "            <word>ldapsuffix</word>\n"
            "            <value>,ou=people,dc=example,dc=com</value>\n"
            "          </option>\n"
            "        </option-list>\n"
            "      </method>\n"
            "    </case2>\n"
            "  </entries-list>\n"
            "</" AUG_TEST_MODULE ">\n");
    free(str);
}

static void
test_store_add(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *entries, *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* add some new list instances */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='7']/case2/method/option-list[_id='5']/"
            "option/word", "myoption", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='7']/case2/method/option-list[_id='5']/"
            "option/value", "assign=", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries-list[_seq='7']/case2/method/option-list[_id='2']",
            0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='8']/type", "local", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='8']/case/database-list[_id='1']/"
            "database", "all", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='8']/case/user-list[_id='1']/user",
            "nobody", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='8']/case/method/value", "sha256", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='4']/case2/method/option-list[_id='1']/"
            "option/word", "cache", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='4']/case2/method/option-list[_id='1']/"
            "option/value", "no", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "9c9\n"
            "< host    all         all         .example.com          md5\n"
            "---\n"
            "> host    all         all         .example.com          md5\tcache=\"no\"\n"
            "14c14,15\n"
            "< host all all .dev.example.com ldap ldapserver=auth.example.com ldaptls=1 ldapprefix=\"uid=\" ldapsuffix=\",ou=people,dc=example,dc=com\"\n"
            "---\n"
            "> host all all .dev.example.com ldap ldapserver=auth.example.com ldaptls=1 myoption=\"assign=\" ldapprefix=\"uid=\"\tldapsuffix=\",ou=people,dc=example,dc=com\"\n"
            "> local\tall\tnobody\tsha256\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='7']/case2/method/option-list[_id='1']/"
            "option/value", "auth5.example.com", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='6']/case2/method/option-list[_id='2']/"
            "option/word", "spec_realm", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entries-list[_seq='2']/case2/address",
            "192.168.0.1/24", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "5c5\n"
            "< host    all         all         127.0.0.1/32          md5\n"
            "---\n"
            "> host    all         all         192.168.0.1/24          md5\n"
            "13,14c13,14\n"
            "< host all all .dev.example.com gss include_realm=0 krb_realm=EXAMPLE.COM map=somemap\n"
            "< host all all .dev.example.com ldap ldapserver=auth.example.com ldaptls=1 ldapprefix=\"uid=\" ldapsuffix=\",ou=people,dc=example,dc=com\"\n"
            "---\n"
            "> host all all .dev.example.com gss include_realm=0 spec_realm=EXAMPLE.COM map=somemap\n"
            "> host all all .dev.example.com ldap ldapserver=auth5.example.com ldaptls=1 ldapprefix=\"uid=\" ldapsuffix=\",ou=people,dc=example,dc=com\"\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries-list[_seq='7']/case2/method/option-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries-list[_seq='7']/case2/method/option-list[_id='4']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entries-list[_seq='3']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "7d6\n"
            "< host    all         all         foo.example.com       md5\n"
            "14c13\n"
            "< host all all .dev.example.com ldap ldapserver=auth.example.com ldaptls=1 ldapprefix=\"uid=\" ldapsuffix=\",ou=people,dc=example,dc=com\"\n"
            "---\n"
            "> host all all .dev.example.com ldap ldapserver=auth.example.com ldapprefix=uid=\n"));
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_load, tteardown),
        cmocka_unit_test_teardown(test_store_add, tteardown),
        cmocka_unit_test_teardown(test_store_modify, tteardown),
        cmocka_unit_test_teardown(test_store_remove, tteardown),
    };

    return cmocka_run_group_tests(tests, setup_f, tteardown_glob);
}
