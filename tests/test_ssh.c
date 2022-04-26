/**
 * @file test_sh.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief ssh SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/ssh"
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

#define AUG_TEST_MODULE "ssh"

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
            "  <ch-send-env-list>\n"
            "    <_id>1</_id>\n"
            "    <other-entry>\n"
            "      <key-re>IdentityFile</key-re>\n"
            "      <value-to-spc>/etc/ssh/identity.asc</value-to-spc>\n"
            "    </other-entry>\n"
            "  </ch-send-env-list>\n"
            "  <config-entries>\n"
            "    <_id>1</_id>\n"
            "    <match>\n"
            "      <match-cond>\n"
            "        <config-entries>\n"
            "          <_id>1</_id>\n"
            "          <node>\n"
            "            <label>final</label>\n"
            "            <value>all</value>\n"
            "          </node>\n"
            "        </config-entries>\n"
            "      </match-cond>\n"
            "      <settings>\n"
            "        <ch-send-env-list>\n"
            "          <_id>1</_id>\n"
            "          <other-entry>\n"
            "            <key-re>GSSAPIAuthentication</key-re>\n"
            "            <value-to-spc>yes</value-to-spc>\n"
            "          </other-entry>\n"
            "        </ch-send-env-list>\n"
            "      </settings>\n"
            "    </match>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <host>\n"
            "      <value-to-eol>suse.cz</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>ForwardAgent</key-re>\n"
            "          <value-to-spc>yes</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>2</_id>\n"
            "        <send-env>\n"
            "          <array-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <array-entry>\n"
            "              <array-entry>1</array-entry>\n"
            "              <value-to-spc>LC_LANG</value-to-spc>\n"
            "            </array-entry>\n"
            "          </array-entry-list>\n"
            "        </send-env>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <host>\n"
            "      <value-to-eol>targaryen</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>HostName</key-re>\n"
            "          <value-to-spc>192.168.1.10</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>2</_id>\n"
            "        <other-entry>\n"
            "          <key-re>User</key-re>\n"
            "          <value-to-spc>daenerys</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>3</_id>\n"
            "        <other-entry>\n"
            "          <key-re>Port</key-re>\n"
            "          <value-to-spc>7654</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>4</_id>\n"
            "        <other-entry>\n"
            "          <key-re>IdentityFile</key-re>\n"
            "          <value-to-spc>~/.ssh/targaryen.key</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>5</_id>\n"
            "        <other-entry>\n"
            "          <key-re>LogLevel</key-re>\n"
            "          <value-to-spc>INFO</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>6</_id>\n"
            "        <other-entry>\n"
            "          <key-re>Compression</key-re>\n"
            "          <value-to-spc>yes</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <host>\n"
            "      <value-to-eol>tyrell</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>HostName</key-re>\n"
            "          <value-to-spc>192.168.10.20</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <host>\n"
            "      <value-to-eol>mail.watzmann.net</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <local-fw>\n"
            "          <node>\n"
            "            <label>11111</label>\n"
            "            <value-to-eol>mail.watzmann.net:110</value-to-eol>\n"
            "          </node>\n"
            "        </local-fw>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <host>\n"
            "      <value-to-eol>martell</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>HostName</key-re>\n"
            "          <value-to-spc>192.168.10.50</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <host>\n"
            "      <value-to-eol>*ell</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>user</key-re>\n"
            "          <value-to-spc>oberyn</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <host>\n"
            "      <value-to-eol>* !martell</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>LogLevel</key-re>\n"
            "          <value-to-spc>INFO</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>2</_id>\n"
            "        <other-entry>\n"
            "          <key-re>ForwardAgent</key-re>\n"
            "          <value-to-spc>yes</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <host>\n"
            "      <value-to-eol>*</value-to-eol>\n"
            "      <ch-send-env-list>\n"
            "        <_id>1</_id>\n"
            "        <other-entry>\n"
            "          <key-re>User</key-re>\n"
            "          <value-to-spc>root</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>2</_id>\n"
            "        <other-entry>\n"
            "          <key-re>Compression</key-re>\n"
            "          <value-to-spc>yes</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>3</_id>\n"
            "        <other-entry>\n"
            "          <key-re>ForwardAgent</key-re>\n"
            "          <value-to-spc>no</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>4</_id>\n"
            "        <other-entry>\n"
            "          <key-re>ForwardX11Trusted</key-re>\n"
            "          <value-to-spc>yes</value-to-spc>\n"
            "        </other-entry>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>5</_id>\n"
            "        <send-env>\n"
            "          <array-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <array-entry>\n"
            "              <array-entry>1</array-entry>\n"
            "              <value-to-spc>LC_IDENTIFICATION</value-to-spc>\n"
            "            </array-entry>\n"
            "          </array-entry-list>\n"
            "          <array-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <array-entry>\n"
            "              <array-entry>2</array-entry>\n"
            "              <value-to-spc>LC_ALL</value-to-spc>\n"
            "            </array-entry>\n"
            "          </array-entry-list>\n"
            "          <array-entry-list>\n"
            "            <_id>3</_id>\n"
            "            <array-entry>\n"
            "              <array-entry>3</array-entry>\n"
            "              <value-to-spc>LC_*</value-to-spc>\n"
            "            </array-entry>\n"
            "          </array-entry-list>\n"
            "        </send-env>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>6</_id>\n"
            "        <proxy-command>ssh -q -W %h:%p gateway.example.com</proxy-command>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>7</_id>\n"
            "        <remote-fw>\n"
            "          <node>\n"
            "            <label>[1.2.3.4]:20023</label>\n"
            "            <value-to-eol>localhost:22</value-to-eol>\n"
            "          </node>\n"
            "        </remote-fw>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>8</_id>\n"
            "        <remote-fw>\n"
            "          <node>\n"
            "            <label>2221</label>\n"
            "            <value-to-eol>lhost1:22</value-to-eol>\n"
            "          </node>\n"
            "        </remote-fw>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>9</_id>\n"
            "        <local-fw>\n"
            "          <node>\n"
            "            <label>3001</label>\n"
            "            <value-to-eol>remotehost:3000</value-to-eol>\n"
            "          </node>\n"
            "        </local-fw>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>10</_id>\n"
            "        <ciphers>\n"
            "          <commas-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>1</commas-entry>\n"
            "              <value-to-comma>aes128-ctr</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>2</commas-entry>\n"
            "              <value-to-comma>aes192-ctr</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "        </ciphers>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>11</_id>\n"
            "        <macs>\n"
            "          <commas-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>1</commas-entry>\n"
            "              <value-to-comma>hmac-md5</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>2</commas-entry>\n"
            "              <value-to-comma>hmac-sha1</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>3</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>3</commas-entry>\n"
            "              <value-to-comma>umac-64@openssh.com</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "        </macs>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>12</_id>\n"
            "        <host-key-algorithms>\n"
            "          <commas-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>1</commas-entry>\n"
            "              <value-to-comma>ssh-ed25519-cert-v01@openssh.com</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>2</commas-entry>\n"
            "              <value-to-comma>ssh-ed25519</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>3</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>3</commas-entry>\n"
            "              <value-to-comma>ssh-rsa-cert-v01@openssh.com</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>4</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>4</commas-entry>\n"
            "              <value-to-comma>ssh-rsa</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "        </host-key-algorithms>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>13</_id>\n"
            "        <kex-algorithms>\n"
            "          <commas-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>1</commas-entry>\n"
            "              <value-to-comma>curve25519-sha256@libssh.org</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>2</commas-entry>\n"
            "              <value-to-comma>diffie-hellman-group-exchange-sha256</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "        </kex-algorithms>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>14</_id>\n"
            "        <pubkey-accepted-key-types>\n"
            "          <commas-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>1</commas-entry>\n"
            "              <value-to-comma>ssh-ed25519-cert-v01@openssh.com</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>2</commas-entry>\n"
            "              <value-to-comma>ssh-ed25519</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>3</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>3</commas-entry>\n"
            "              <value-to-comma>ssh-rsa-cert-v01@openssh.com</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "          <commas-entry-list>\n"
            "            <_id>4</_id>\n"
            "            <commas-entry>\n"
            "              <commas-entry>4</commas-entry>\n"
            "              <value-to-comma>ssh-rsa</value-to-comma>\n"
            "            </commas-entry>\n"
            "          </commas-entry-list>\n"
            "        </pubkey-accepted-key-types>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>15</_id>\n"
            "        <global-knownhosts-file>\n"
            "          <spaces-entry-list>\n"
            "            <_id>1</_id>\n"
            "            <spaces-entry>\n"
            "              <spaces-entry>1</spaces-entry>\n"
            "              <value-to-spc>/etc/ssh/ssh_known_hosts</value-to-spc>\n"
            "            </spaces-entry>\n"
            "          </spaces-entry-list>\n"
            "          <spaces-entry-list>\n"
            "            <_id>2</_id>\n"
            "            <spaces-entry>\n"
            "              <spaces-entry>2</spaces-entry>\n"
            "              <value-to-spc>/etc/ssh/ssh_known_hosts2</value-to-spc>\n"
            "            </spaces-entry>\n"
            "          </spaces-entry-list>\n"
            "        </global-knownhosts-file>\n"
            "      </ch-send-env-list>\n"
            "      <ch-send-env-list>\n"
            "        <_id>16</_id>\n"
            "        <rekey-limit>\n"
            "          <amount>1G</amount>\n"
            "          <duration>1h</duration>\n"
            "        </rekey-limit>\n"
            "      </ch-send-env-list>\n"
            "    </host>\n"
            "  </config-entries>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/host/ch-send-env-list[_id='2']/"
            "other-entry/key-re", "AddKeysToAgent", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='6']/host/ch-send-env-list[_id='2']/"
            "other-entry/value-to-spc", "confirm1h30m", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='6']/host/ch-send-env-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_before(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/host/ch-send-env-list[_id='3']/"
            "other-entry/key-re", "PermitRemoteOpen", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='2']/host/ch-send-env-list[_id='3']/"
            "other-entry/value-to-spc", ":830", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='2']/host/ch-send-env-list[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/host/ch-send-env-list[_id='15']/"
            "global-knownhosts-file/spaces-entry-list[_id='3']/spaces-entry/spaces-entry", "3", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/host/ch-send-env-list[_id='15']/"
            "global-knownhosts-file/spaces-entry-list[_id='3']/spaces-entry/value-to-spc", "/root/.ssh/ssh_known_host",
            0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "8a9\n"
            "> PermitRemoteOpen :830\n"
            "25a27\n"
            "> AddKeysToAgent confirm1h30m\n"
            "52c54\n"
            "< GlobalKnownHostsFile /etc/ssh/ssh_known_hosts /etc/ssh/ssh_known_hosts2\n"
            "---\n"
            "> GlobalKnownHostsFile /etc/ssh/ssh_known_hosts /etc/ssh/ssh_known_hosts2 /root/.ssh/ssh_known_host\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='3']/host/ch-send-env-list[_id='5']/"
            "other-entry/value-to-spc", "DEBUG3", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/host/ch-send-env-list[_id='10']/"
            "ciphers/commas-entry-list[_id='2']/commas-entry/value-to-comma", "aes256-ctr", LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='9']/host/ch-send-env-list[_id='16']/"
            "rekey-limit/amount", "500M", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "16c16\n"
            "<     LogLevel INFO\n"
            "---\n"
            ">     LogLevel DEBUG3\n"
            "47c47\n"
            "< Ciphers aes128-ctr,aes192-ctr\n"
            "---\n"
            "> Ciphers aes128-ctr,aes256-ctr\n"
            "53c53\n"
            "< RekeyLimit 1G 1h\n"
            "---\n"
            "> RekeyLimit 500M 1h\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='9']/host/ch-send-env-list[_id='14']/"
            "pubkey-accepted-key-types/commas-entry-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='7']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='9']/host/ch-send-env-list[_id='16']/"
            "rekey-limit/duration", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "28,30d27\n"
            "< Host *ell\n"
            "<     user oberyn\n"
            "< \n"
            "51c48\n"
            "< PubkeyAcceptedKeyTypes ssh-ed25519-cert-v01@openssh.com,ssh-ed25519,ssh-rsa-cert-v01@openssh.com,ssh-rsa\n"
            "---\n"
            "> PubkeyAcceptedKeyTypes ssh-ed25519-cert-v01@openssh.com,ssh-rsa-cert-v01@openssh.com,ssh-rsa\n"
            "53c50\n"
            "< RekeyLimit 1G 1h\n"
            "---\n"
            "> RekeyLimit 1G\n"));
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
