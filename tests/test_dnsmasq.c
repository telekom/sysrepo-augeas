/**
 * @file test_dnsmasq.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief dnsmasq SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/dnsmasq"
#include "srds_augeas.c"

#include <assert.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <unistd.h>

#include <cmocka.h>
#include <libyang/libyang.h>
#include <sysrepo/plugins_datastore.h>

#define AUG_TEST_MODULE "dnsmasq"

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
            "  <config-entries>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <entry-re>local-service</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <entry-re>port</entry-re>\n"
            "      <sto-to-eol>5353</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <entry-re>domain-needed</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <entry-re>bogus-priv</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <entry-re>conf-file</entry-re>\n"
            "      <sto-to-eol>/etc/dnsmasq.d/trust-anchors.conf</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <entry-re>dnssec</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <entry-re>dnssec-check-unsigned</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <entry-re>filterwin2k</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <entry>\n"
            "      <entry-re>resolv-file</entry-re>\n"
            "      <sto-to-eol>/usr/etc/resolv.conf</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <entry-re>strict-order</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <entry-re>no-resolv</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <entry-re>no-poll</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>13</_id>\n"
            "    <server>\n"
            "      <value>192.168.0.1</value>\n"
            "      <domain>localnet</domain>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>14</_id>\n"
            "    <server>\n"
            "      <value>10.1.2.3</value>\n"
            "      <domain>3.168.192.in-addr.arpa</domain>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>15</_id>\n"
            "    <entry>\n"
            "      <entry-re>local</entry-re>\n"
            "      <sto-to-eol>/localnet/</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>16</_id>\n"
            "    <address>\n"
            "      <sto-no-slash>127.0.0.1</sto-no-slash>\n"
            "      <domain>double-click.net</domain>\n"
            "    </address>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>17</_id>\n"
            "    <address>\n"
            "      <sto-no-slash>fe80::20d:60ff:fe36:f83</sto-no-slash>\n"
            "      <domain>www.thekelleys.org.uk</domain>\n"
            "    </address>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>18</_id>\n"
            "    <entry>\n"
            "      <entry-re>ipset</entry-re>\n"
            "      <sto-to-eol>/yahoo.com/google.com/vpn,search</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>19</_id>\n"
            "    <server>\n"
            "      <value>10.1.2.3</value>\n"
            "      <source>\n"
            "        <value>eth1</value>\n"
            "      </source>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>20</_id>\n"
            "    <server>\n"
            "      <value>10.1.2.3</value>\n"
            "      <source>\n"
            "        <value>192.168.1.1</value>\n"
            "        <port>55</port>\n"
            "      </source>\n"
            "    </server>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>21</_id>\n"
            "    <entry>\n"
            "      <entry-re>user</entry-re>\n"
            "      <sto-to-eol>nobody</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>22</_id>\n"
            "    <entry>\n"
            "      <entry-re>group</entry-re>\n"
            "      <sto-to-eol>none</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>23</_id>\n"
            "    <entry>\n"
            "      <entry-re>interface</entry-re>\n"
            "      <sto-to-eol>eth0</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>24</_id>\n"
            "    <entry>\n"
            "      <entry-re>except-interface</entry-re>\n"
            "      <sto-to-eol>loopback</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>25</_id>\n"
            "    <entry>\n"
            "      <entry-re>listen-address</entry-re>\n"
            "      <sto-to-eol>127.0.0.1</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>26</_id>\n"
            "    <entry>\n"
            "      <entry-re>bind-interfaces</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>27</_id>\n"
            "    <entry>\n"
            "      <entry-re>no-hosts</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>28</_id>\n"
            "    <entry>\n"
            "      <entry-re>addn-hosts</entry-re>\n"
            "      <sto-to-eol>/etc/banner_add_hosts</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>29</_id>\n"
            "    <entry>\n"
            "      <entry-re>expand-hosts</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>30</_id>\n"
            "    <entry>\n"
            "      <entry-re>domain</entry-re>\n"
            "      <sto-to-eol>thekelleys.org.uk</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>31</_id>\n"
            "    <entry>\n"
            "      <entry-re>domain</entry-re>\n"
            "      <sto-to-eol>wireless.thekelleys.org.uk,192.168.2.0/24</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>32</_id>\n"
            "    <entry>\n"
            "      <entry-re>domain</entry-re>\n"
            "      <sto-to-eol>reserved.thekelleys.org.uk,192.68.3.100,192.168.3.200</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>33</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-range</entry-re>\n"
            "      <sto-to-eol>1234::2, 1234::500, 64, 12h</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>34</_id>\n"
            "    <entry>\n"
            "      <entry-re>enable-ra</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>35</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-host</entry-re>\n"
            "      <sto-to-eol>id:00:01:00:01:16:d2:83:fc:92:d4:19:e2:d8:b2, fred, [1234::5]</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>36</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-ignore</entry-re>\n"
            "      <sto-to-eol>tag:!known</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>37</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-vendorclass</entry-re>\n"
            "      <sto-to-eol>set:red,Linux</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>38</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-userclass</entry-re>\n"
            "      <sto-to-eol>set:red,accounts</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>39</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-mac</entry-re>\n"
            "      <sto-to-eol>set:red,00:60:8C:*:*:*</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>40</_id>\n"
            "    <entry>\n"
            "      <entry-re>read-ethers</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>41</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>option6:dns-server,[1234::77],[1234::88]</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>42</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>option6:information-refresh-time,6h</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>43</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>40,welly</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>44</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>128,e4:45:74:68:00:00</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>45</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>129,NIC=eepro100</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>46</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>252,\"\\n\"</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>47</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-option</entry-re>\n"
            "      <sto-to-eol>vendor:Etherboot,60,\"Etherboot\"</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>48</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-boot</entry-re>\n"
            "      <sto-to-eol>undionly.kpxe</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>49</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-match</entry-re>\n"
            "      <sto-to-eol>set:ipxe,175 # iPXE sends a 175 option.</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>50</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-boot</entry-re>\n"
            "      <sto-to-eol>tag:ipxe,http://boot.ipxe.org/demo/boot.php</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>51</_id>\n"
            "    <entry>\n"
            "      <entry-re>pxe-prompt</entry-re>\n"
            "      <sto-to-eol>\"What system shall I netboot?\"</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>52</_id>\n"
            "    <entry>\n"
            "      <entry-re>pxe-service</entry-re>\n"
            "      <sto-to-eol>x86PC, \"Install windows from RIS server\", 1, 1.2.3.4</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>53</_id>\n"
            "    <entry>\n"
            "      <entry-re>enable-tftp</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>54</_id>\n"
            "    <entry>\n"
            "      <entry-re>tftp-root</entry-re>\n"
            "      <sto-to-eol>/var/ftpd</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>55</_id>\n"
            "    <entry>\n"
            "      <entry-re>tftp-no-fail</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>56</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-lease-max</entry-re>\n"
            "      <sto-to-eol>150</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>57</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-leasefile</entry-re>\n"
            "      <sto-to-eol>/var/lib/misc/dnsmasq.leases</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>58</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-authoritative</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>59</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-rapid-commit</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>60</_id>\n"
            "    <entry>\n"
            "      <entry-re>cache-size</entry-re>\n"
            "      <sto-to-eol>150</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>61</_id>\n"
            "    <entry>\n"
            "      <entry-re>no-negcache</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>62</_id>\n"
            "    <entry>\n"
            "      <entry-re>bogus-nxdomain</entry-re>\n"
            "      <sto-to-eol>64.94.110.11</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>63</_id>\n"
            "    <entry>\n"
            "      <entry-re>alias</entry-re>\n"
            "      <sto-to-eol>192.168.0.10-192.168.0.40,10.0.0.0,255.255.255.0</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>64</_id>\n"
            "    <entry>\n"
            "      <entry-re>mx-host</entry-re>\n"
            "      <sto-to-eol>maildomain.com,servermachine.com,50</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>65</_id>\n"
            "    <entry>\n"
            "      <entry-re>mx-target</entry-re>\n"
            "      <sto-to-eol>servermachine.com</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>66</_id>\n"
            "    <entry>\n"
            "      <entry-re>localmx</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>67</_id>\n"
            "    <entry>\n"
            "      <entry-re>selfmx</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>68</_id>\n"
            "    <entry>\n"
            "      <entry-re>srv-host</entry-re>\n"
            "      <sto-to-eol>_ldap._tcp.example.com,ldapserver.example.com,389</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>69</_id>\n"
            "    <entry>\n"
            "      <entry-re>ptr-record</entry-re>\n"
            "      <sto-to-eol>_http._tcp.dns-sd-services,\"New Employee Page._http._tcp.dns-sd-services\"</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>70</_id>\n"
            "    <entry>\n"
            "      <entry-re>txt-record</entry-re>\n"
            "      <sto-to-eol>_http._tcp.example.com,name=value,paper=A4</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>71</_id>\n"
            "    <entry>\n"
            "      <entry-re>cname</entry-re>\n"
            "      <sto-to-eol>bertand,bert</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>72</_id>\n"
            "    <entry>\n"
            "      <entry-re>log-queries</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>73</_id>\n"
            "    <entry>\n"
            "      <entry-re>log-dhcp</entry-re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>74</_id>\n"
            "    <entry>\n"
            "      <entry-re>conf-dir</entry-re>\n"
            "      <sto-to-eol>/etc/dnsmasq.d,.bak</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>75</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-name-match</entry-re>\n"
            "      <sto-to-eol>set:wpad-ignore,wpad</sto-to-eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>76</_id>\n"
            "    <entry>\n"
            "      <entry-re>dhcp-ignore-names</entry-re>\n"
            "      <sto-to-eol>tag:wpad-ignore</sto-to-eol>\n"
            "    </entry>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='77']/server/value",
            "127.0.0.1", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='77']/server/domain",
            "localhost.myhome.com", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='77']/server/domain",
            "localhost2.myhome.com", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='77']/server/port",
            "1001", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='14']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='19']/server/source/port",
            "1056", 0, &entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='78']/entry/entry-re",
            "dhcp-option", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='78']/entry/sto-to-eol",
            "some_special_option", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='47']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "21a22\n"
            "> server=/localhost.myhome.com/localhost2.myhome.com/127.0.0.1#1001\n"
            "28c29\n"
            "< server=10.1.2.3@eth1\n"
            "---\n"
            "> server=10.1.2.3@eth1#1056\n"
            "62a64\n"
            "> dhcp-option=some_special_option"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='13']/server/domain[.='localnet']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='13']/server/domain", "mynet", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='19']/server/source/value", "eth0",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='63']/entry/sto-to-eol",
            "192.168.0.10-192.168.0.40", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='71']/entry/entry-re",
            "hname", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "20c20\n"
            "< server=/localnet/192.168.0.1\n"
            "---\n"
            "> server=/mynet/192.168.0.1\n"
            "28c28\n"
            "< server=10.1.2.3@eth1\n"
            "---\n"
            "> server=10.1.2.3@eth0\n"
            "80c80\n"
            "< alias=192.168.0.10-192.168.0.40,10.0.0.0,255.255.255.0\n"
            "---\n"
            "> alias=192.168.0.10-192.168.0.40\n"
            "91c91\n"
            "< cname=bertand,bert\n"
            "---\n"
            "> hname=bertand,bert"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='9']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='14']/server/domain[.='3.168.192.in-addr.arpa']",
            0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "config-entries[_id='20']/server/source/port", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "15d14\n"
            "< resolv-file=/usr/etc/resolv.conf\n"
            "21c20\n"
            "< server=/3.168.192.in-addr.arpa/10.1.2.3\n"
            "---\n"
            "> server=10.1.2.3\n"
            "29c28\n"
            "< server=10.1.2.3@192.168.1.1#55\n"
            "---\n"
            "> server=10.1.2.3@192.168.1.1"));
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
