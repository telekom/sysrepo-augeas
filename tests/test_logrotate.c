/**
 * @file test_logrotate.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief logrotate SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/logrotate"
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

#define AUG_TEST_MODULE "logrotate"

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
            "  <attrs>\n"
            "    <_id>1</_id>\n"
            "    <schedule>weekly</schedule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>2</_id>\n"
            "    <rotate>4</rotate>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>3</_id>\n"
            "    <create/>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>4</_id>\n"
            "    <tabooext>\n"
            "      <value>+</value>\n"
            "      <list-item-list>\n"
            "        <_id>1</_id>\n"
            "        <list-item>.old</list-item>\n"
            "      </list-item-list>\n"
            "      <list-item-list>\n"
            "        <_id>2</_id>\n"
            "        <list-item>.orig</list-item>\n"
            "      </list-item-list>\n"
            "      <list-item-list>\n"
            "        <_id>3</_id>\n"
            "        <list-item>.ignore</list-item>\n"
            "      </list-item-list>\n"
            "    </tabooext>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>5</_id>\n"
            "    <include>/etc/logrotate.d</include>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>6</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/wtmp</file>\n"
            "      </file-list>\n"
            "      <file-list>\n"
            "        <_id>2</_id>\n"
            "        <file>/var/log/wtmp2</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <schedule>monthly</schedule>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <create>\n"
            "          <mode>664</mode>\n"
            "          <owner>root</owner>\n"
            "          <group>utmp</group>\n"
            "        </create>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <rotate>1</rotate>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>7</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/btmp</file>\n"
            "      </file-list>\n"
            "      <file-list>\n"
            "        <_id>2</_id>\n"
            "        <file>/var/log/btmp*</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <schedule>monthly</schedule>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <create>\n"
            "          <mode>664</mode>\n"
            "          <owner>root</owner>\n"
            "          <group>utmp</group>\n"
            "        </create>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <rotate>1</rotate>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>8</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/vsftpd.log</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <compress>nocompress</compress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <ifempty>notifempty</ifempty>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <rotate>4</rotate>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <schedule>weekly</schedule>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>9</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/apache2/*.log</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <schedule>weekly</schedule>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <rotate>52</rotate>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <compress>compress</compress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <delaycompress>delaycompress</delaycompress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <ifempty>notifempty</ifempty>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <create>\n"
            "          <mode>640</mode>\n"
            "          <owner>root</owner>\n"
            "          <group>adm</group>\n"
            "        </create>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>8</_id>\n"
            "        <sharedscripts>sharedscripts</sharedscripts>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>9</_id>\n"
            "        <prerotate>                if [ -f /var/run/apache2.pid ]; then\n"
            "                        /etc/init.d/apache2 restart &gt; /dev/null\n"
            "                fi</prerotate>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>10</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/mailman/digest</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <su>\n"
            "          <owner>root</owner>\n"
            "          <group>list</group>\n"
            "        </su>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <schedule>monthly</schedule>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <create>\n"
            "          <mode>664</mode>\n"
            "          <owner>list</owner>\n"
            "          <group>list</group>\n"
            "        </create>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <rotate>4</rotate>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <compress>compress</compress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <delaycompress>delaycompress</delaycompress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>8</_id>\n"
            "        <sharedscripts>sharedscripts</sharedscripts>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>9</_id>\n"
            "        <postrotate>        [ -f '/var/run/mailman/mailman.pid' ] &amp;&amp; /usr/lib/mailman/bin/mailmanctl -q reopen || exit 0</postrotate>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
            "  <attrs>\n"
            "    <_id>11</_id>\n"
            "    <rule>\n"
            "      <file-list>\n"
            "        <_id>1</_id>\n"
            "        <file>/var/log/ntp</file>\n"
            "      </file-list>\n"
            "      <config-entries>\n"
            "        <_id>1</_id>\n"
            "        <compress>compress</compress>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>2</_id>\n"
            "        <dateext>dateext</dateext>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>3</_id>\n"
            "        <maxage>365</maxage>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>4</_id>\n"
            "        <rotate>99</rotate>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>5</_id>\n"
            "        <size>+2048k</size>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>6</_id>\n"
            "        <ifempty>notifempty</ifempty>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>7</_id>\n"
            "        <missingok>missingok</missingok>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>8</_id>\n"
            "        <copytruncate>copytruncate</copytruncate>\n"
            "      </config-entries>\n"
            "      <config-entries>\n"
            "        <_id>9</_id>\n"
            "        <postrotate>        chmod 644 /var/log/ntp</postrotate>\n"
            "      </config-entries>\n"
            "    </rule>\n"
            "  </attrs>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='11']/rule/config-entries[_id='10']/shred",
            "noshred", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "attrs[_id='11']/rule/config-entries[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='9']/rule/file-list[_id='2']/file",
            "/usr/local/var/log/apache2/*.log", 0, &entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='12']/rule/file-list[_id='1']/file",
            "/root_file", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='12']/rule/config-entries[_id='1']/"
            "su", NULL, 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='12']/rule/config-entries[_id='2']/"
            "olddir", "/root_old_dir", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='12']/rule/config-entries[_id='2']/"
            "copy", "copy", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='12']/rule/config-entries[_id='2']/"
            "start", "123456789", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "attrs[_id='9']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "45c45\n"
            "< /var/log/apache2/*.log {\n"
            "---\n"
            "> /var/log/apache2/*.log \"/usr/local/var/log/apache2/*.log\" {\n"
            "59a60,66\n"
            "> \"/root_file\"\n"
            "> {\n"
            "> \tsu\n"
            "> \tstart 123456789\n"
            "> \tcopy\n"
            "> \tolddir /root_old_dir\n"
            "> }\n"
            "76a84\n"
            "> \tnoshred\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='11']/rule/config-entries[_id='3']/"
            "maxage", "182", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='6']/rule/config-entries[_id='3']/"
            "create/group", "root", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "attrs[_id='2']/rotate", "2", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "6c6\n"
            "< rotate 4\n"
            "---\n"
            "> rotate 2\n"
            "25c25\n"
            "<     create 0664 root utmp\n"
            "---\n"
            ">     create 0664 root root\n"
            "78c78\n"
            "< \tmaxage 365\n"
            "---\n"
            "> \tmaxage 182\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "attrs[_id='4']/tabooext/list-item-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "attrs[_id='10']/rule/config-entries[_id='1']/su/group", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "attrs[_id='11']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "14c14\n"
            "< tabooext + .old .orig .ignore\n"
            "---\n"
            "> tabooext + .old .ignore\n"
            "62c62\n"
            "< \tsu root list\n"
            "---\n"
            "> \tsu root\n"
            "72,85d71\n"
            "< \tendscript\n"
            "< }\n"
            "< \"/var/log/ntp\"\n"
            "< {\n"
            "< \tcompress\n"
            "< \tdateext\n"
            "< \tmaxage 365\n"
            "< \trotate 99\n"
            "< \tsize +2048k\n"
            "< \tnotifempty\n"
            "< \tmissingok\n"
            "< \tcopytruncate\n"
            "< \tpostrotate\n"
            "<         chmod 644 /var/log/ntp\n"));
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
