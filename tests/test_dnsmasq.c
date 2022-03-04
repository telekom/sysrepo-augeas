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

static int
setup_f(void **state)
{
    return tsetup_glob(state, "dnsmasq", &srpds__, AUG_TEST_INPUT_FILES);
}

static void
test_load(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    char *str;

    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));
    lyd_print_mem(&str, st->data, LYD_XML, LYD_PRINT_WITHSIBLINGS);

    assert_string_equal(str,
            "<dnsmasq xmlns=\"aug:dnsmasq\">\n"
            "  <config-file>" AUG_CONFIG_FILES_DIR "/dnsmasq</config-file>\n"
            "  <config-entries>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <entry_re>local-service</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <entry_re>port</entry_re>\n"
            "      <sto_to_eol>5353</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <entry_re>domain-needed</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <entry_re>bogus-priv</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <entry_re>conf-file</entry_re>\n"
            "      <sto_to_eol>/etc/dnsmasq.d/trust-anchors.conf</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <entry_re>dnssec</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <entry_re>dnssec-check-unsigned</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <entry_re>filterwin2k</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>9</_id>\n"
            "    <entry>\n"
            "      <entry_re>resolv-file</entry_re>\n"
            "      <sto_to_eol>/usr/etc/resolv.conf</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <entry_re>strict-order</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <entry_re>no-resolv</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <entry_re>no-poll</entry_re>\n"
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
            "      <entry_re>local</entry_re>\n"
            "      <sto_to_eol>/localnet/</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>16</_id>\n"
            "    <address>\n"
            "      <sto_no_slash>127.0.0.1</sto_no_slash>\n"
            "      <domain>double-click.net</domain>\n"
            "    </address>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>17</_id>\n"
            "    <address>\n"
            "      <sto_no_slash>fe80::20d:60ff:fe36:f83</sto_no_slash>\n"
            "      <domain>www.thekelleys.org.uk</domain>\n"
            "    </address>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>18</_id>\n"
            "    <entry>\n"
            "      <entry_re>ipset</entry_re>\n"
            "      <sto_to_eol>/yahoo.com/google.com/vpn,search</sto_to_eol>\n"
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
            "      <entry_re>user</entry_re>\n"
            "      <sto_to_eol>nobody</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>22</_id>\n"
            "    <entry>\n"
            "      <entry_re>group</entry_re>\n"
            "      <sto_to_eol>none</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>23</_id>\n"
            "    <entry>\n"
            "      <entry_re>interface</entry_re>\n"
            "      <sto_to_eol>eth0</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>24</_id>\n"
            "    <entry>\n"
            "      <entry_re>except-interface</entry_re>\n"
            "      <sto_to_eol>loopback</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>25</_id>\n"
            "    <entry>\n"
            "      <entry_re>listen-address</entry_re>\n"
            "      <sto_to_eol>127.0.0.1</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>26</_id>\n"
            "    <entry>\n"
            "      <entry_re>bind-interfaces</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>27</_id>\n"
            "    <entry>\n"
            "      <entry_re>no-hosts</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>28</_id>\n"
            "    <entry>\n"
            "      <entry_re>addn-hosts</entry_re>\n"
            "      <sto_to_eol>/etc/banner_add_hosts</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>29</_id>\n"
            "    <entry>\n"
            "      <entry_re>expand-hosts</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>30</_id>\n"
            "    <entry>\n"
            "      <entry_re>domain</entry_re>\n"
            "      <sto_to_eol>thekelleys.org.uk</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>31</_id>\n"
            "    <entry>\n"
            "      <entry_re>domain</entry_re>\n"
            "      <sto_to_eol>wireless.thekelleys.org.uk,192.168.2.0/24</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>32</_id>\n"
            "    <entry>\n"
            "      <entry_re>domain</entry_re>\n"
            "      <sto_to_eol>reserved.thekelleys.org.uk,192.68.3.100,192.168.3.200</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>33</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-range</entry_re>\n"
            "      <sto_to_eol>1234::2, 1234::500, 64, 12h</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>34</_id>\n"
            "    <entry>\n"
            "      <entry_re>enable-ra</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>35</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-host</entry_re>\n"
            "      <sto_to_eol>id:00:01:00:01:16:d2:83:fc:92:d4:19:e2:d8:b2, fred, [1234::5]</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>36</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-ignore</entry_re>\n"
            "      <sto_to_eol>tag:!known</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>37</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-vendorclass</entry_re>\n"
            "      <sto_to_eol>set:red,Linux</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>38</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-userclass</entry_re>\n"
            "      <sto_to_eol>set:red,accounts</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>39</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-mac</entry_re>\n"
            "      <sto_to_eol>set:red,00:60:8C:*:*:*</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>40</_id>\n"
            "    <entry>\n"
            "      <entry_re>read-ethers</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>41</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>option6:dns-server,[1234::77],[1234::88]</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>42</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>option6:information-refresh-time,6h</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>43</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>40,welly</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>44</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>128,e4:45:74:68:00:00</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>45</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>129,NIC=eepro100</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>46</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>252,\"\\n\"</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>47</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-option</entry_re>\n"
            "      <sto_to_eol>vendor:Etherboot,60,\"Etherboot\"</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>48</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-boot</entry_re>\n"
            "      <sto_to_eol>undionly.kpxe</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>49</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-match</entry_re>\n"
            "      <sto_to_eol>set:ipxe,175 # iPXE sends a 175 option.</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>50</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-boot</entry_re>\n"
            "      <sto_to_eol>tag:ipxe,http://boot.ipxe.org/demo/boot.php</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>51</_id>\n"
            "    <entry>\n"
            "      <entry_re>pxe-prompt</entry_re>\n"
            "      <sto_to_eol>\"What system shall I netboot?\"</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>52</_id>\n"
            "    <entry>\n"
            "      <entry_re>pxe-service</entry_re>\n"
            "      <sto_to_eol>x86PC, \"Install windows from RIS server\", 1, 1.2.3.4</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>53</_id>\n"
            "    <entry>\n"
            "      <entry_re>enable-tftp</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>54</_id>\n"
            "    <entry>\n"
            "      <entry_re>tftp-root</entry_re>\n"
            "      <sto_to_eol>/var/ftpd</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>55</_id>\n"
            "    <entry>\n"
            "      <entry_re>tftp-no-fail</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>56</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-lease-max</entry_re>\n"
            "      <sto_to_eol>150</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>57</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-leasefile</entry_re>\n"
            "      <sto_to_eol>/var/lib/misc/dnsmasq.leases</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>58</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-authoritative</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>59</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-rapid-commit</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>60</_id>\n"
            "    <entry>\n"
            "      <entry_re>cache-size</entry_re>\n"
            "      <sto_to_eol>150</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>61</_id>\n"
            "    <entry>\n"
            "      <entry_re>no-negcache</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>62</_id>\n"
            "    <entry>\n"
            "      <entry_re>bogus-nxdomain</entry_re>\n"
            "      <sto_to_eol>64.94.110.11</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>63</_id>\n"
            "    <entry>\n"
            "      <entry_re>alias</entry_re>\n"
            "      <sto_to_eol>192.168.0.10-192.168.0.40,10.0.0.0,255.255.255.0</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>64</_id>\n"
            "    <entry>\n"
            "      <entry_re>mx-host</entry_re>\n"
            "      <sto_to_eol>maildomain.com,servermachine.com,50</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>65</_id>\n"
            "    <entry>\n"
            "      <entry_re>mx-target</entry_re>\n"
            "      <sto_to_eol>servermachine.com</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>66</_id>\n"
            "    <entry>\n"
            "      <entry_re>localmx</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>67</_id>\n"
            "    <entry>\n"
            "      <entry_re>selfmx</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>68</_id>\n"
            "    <entry>\n"
            "      <entry_re>srv-host</entry_re>\n"
            "      <sto_to_eol>_ldap._tcp.example.com,ldapserver.example.com,389</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>69</_id>\n"
            "    <entry>\n"
            "      <entry_re>ptr-record</entry_re>\n"
            "      <sto_to_eol>_http._tcp.dns-sd-services,\"New Employee Page._http._tcp.dns-sd-services\"</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>70</_id>\n"
            "    <entry>\n"
            "      <entry_re>txt-record</entry_re>\n"
            "      <sto_to_eol>_http._tcp.example.com,name=value,paper=A4</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>71</_id>\n"
            "    <entry>\n"
            "      <entry_re>cname</entry_re>\n"
            "      <sto_to_eol>bertand,bert</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>72</_id>\n"
            "    <entry>\n"
            "      <entry_re>log-queries</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>73</_id>\n"
            "    <entry>\n"
            "      <entry_re>log-dhcp</entry_re>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>74</_id>\n"
            "    <entry>\n"
            "      <entry_re>conf-dir</entry_re>\n"
            "      <sto_to_eol>/etc/dnsmasq.d,.bak</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>75</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-name-match</entry_re>\n"
            "      <sto_to_eol>set:wpad-ignore,wpad</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "  <config-entries>\n"
            "    <_id>76</_id>\n"
            "    <entry>\n"
            "      <entry_re>dhcp-ignore-names</entry_re>\n"
            "      <sto_to_eol>tag:wpad-ignore</sto_to_eol>\n"
            "    </entry>\n"
            "  </config-entries>\n"
            "</dnsmasq>\n");
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

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='78']/entry/entry_re",
            "dhcp-option", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='78']/entry/sto_to_eol",
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "config-entries[_id='63']/entry/sto_to_eol",
            "192.168.0.10-192.168.0.40", LYD_NEW_PATH_UPDATE, NULL));

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
            "> alias=192.168.0.10-192.168.0.40"));
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
