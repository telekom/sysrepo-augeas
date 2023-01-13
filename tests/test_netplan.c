/**
 * @file test_netplan.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief netplan SR DS plugin test
 *
 * @copyright
 * Copyright (c) 2023 Deutsche Telekom AG.
 * Copyright (c) 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "tconfig.h"

/* augeas SR DS plugin */
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/netplan"
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

#define AUG_TEST_MODULE "netplan"

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
            "  <network>\n"
            "    <config-entries>\n"
            "      <_id>1</_id>\n"
            "      <ethernets>\n"
            "        <ethernets>\n"
            "          <_id>1</_id>\n"
            "          <identifier>\n"
            "            <identifier>eth0</identifier>\n"
            "            <ethernet-opts>\n"
            "              <_id>1</_id>\n"
            "              <match>\n"
            "                <match-opts-lines>\n"
            "                  <_id>1</_id>\n"
            "                  <name>\"ESCAPED\\a\\\"'nd\"</name>\n"
            "                </match-opts-lines>\n"
            "                <match-opts-lines>\n"
            "                  <_id>2</_id>\n"
            "                  <macaddress>01:23:45:67:A0:C0</macaddress>\n"
            "                </match-opts-lines>\n"
            "                <match-opts-lines>\n"
            "                  <_id>3</_id>\n"
            "                  <driver>\n"
            "                    <driver-list>\n"
            "                      <_seq>1</_seq>\n"
            "                      <scalar>driv1</scalar>\n"
            "                    </driver-list>\n"
            "                    <driver-list>\n"
            "                      <_seq>2</_seq>\n"
            "                      <scalar>driv2</scalar>\n"
            "                    </driver-list>\n"
            "                  </driver>\n"
            "                </match-opts-lines>\n"
            "              </match>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>2</_id>\n"
            "              <set-name>my-name</set-name>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>3</_id>\n"
            "              <wakeonlan>false</wakeonlan>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>4</_id>\n"
            "              <generic-segmentation-offload>false</generic-segmentation-offload>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>5</_id>\n"
            "              <openvswitch>\n"
            "                <openvswitch-opts-lines>\n"
            "                  <_id>1</_id>\n"
            "                  <lacp>passive</lacp>\n"
            "                </openvswitch-opts-lines>\n"
            "                <openvswitch-opts-lines>\n"
            "                  <_id>2</_id>\n"
            "                  <controller>\n"
            "                    <controller-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <addresses-f>\n"
            "                        <addresses-list>\n"
            "                          <_seq>1</_seq>\n"
            "                          <scalar>tcp:127.0.0.1:6653</scalar>\n"
            "                        </addresses-list>\n"
            "                        <addresses-list>\n"
            "                          <_seq>2</_seq>\n"
            "                          <scalar>\"ssl:[fe80::1234%eth0]:6653\"</scalar>\n"
            "                        </addresses-list>\n"
            "                      </addresses-f>\n"
            "                    </controller-opts-lines>\n"
            "                  </controller>\n"
            "                </openvswitch-opts-lines>\n"
            "                <openvswitch-opts-lines>\n"
            "                  <_id>3</_id>\n"
            "                  <ports>\n"
            "                    <ports-list>\n"
            "                      <_seq>1</_seq>\n"
            "                      <ports-list>\n"
            "                        <_seq>1</_seq>\n"
            "                        <scalar>patch0-1</scalar>\n"
            "                      </ports-list>\n"
            "                      <ports-list>\n"
            "                        <_seq>2</_seq>\n"
            "                        <scalar>patch1-0</scalar>\n"
            "                      </ports-list>\n"
            "                    </ports-list>\n"
            "                    <ports-list>\n"
            "                      <_seq>3</_seq>\n"
            "                      <ports-list>\n"
            "                        <_seq>1</_seq>\n"
            "                        <scalar>patch0-2</scalar>\n"
            "                      </ports-list>\n"
            "                      <ports-list>\n"
            "                        <_seq>2</_seq>\n"
            "                        <scalar>patch2-0</scalar>\n"
            "                      </ports-list>\n"
            "                    </ports-list>\n"
            "                  </ports>\n"
            "                </openvswitch-opts-lines>\n"
            "                <openvswitch-opts-lines>\n"
            "                  <_id>4</_id>\n"
            "                  <ssl>\n"
            "                    <ssl-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <ca-cert>/path/to/file</ca-cert>\n"
            "                    </ssl-opts-lines>\n"
            "                  </ssl>\n"
            "                </openvswitch-opts-lines>\n"
            "              </openvswitch>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>6</_id>\n"
            "              <dhcp4>true</dhcp4>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>7</_id>\n"
            "              <addresses>\n"
            "                <addresses-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <addresses-lines>\n"
            "                    <identifier>24\"</identifier>\n"
            "                    <config-entries>\n"
            "                      <_id>1</_id>\n"
            "                      <lifetime>0</lifetime>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>2</_id>\n"
            "                      <label>\"maas\"</label>\n"
            "                    </config-entries>\n"
            "                  </addresses-lines>\n"
            "                </addresses-list>\n"
            "                <addresses-list>\n"
            "                  <_seq>2</_seq>\n"
            "                  <addresses-lines>\n"
            "                    <identifier>64\"</identifier>\n"
            "                  </addresses-lines>\n"
            "                </addresses-list>\n"
            "              </addresses>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>8</_id>\n"
            "              <nameservers>\n"
            "                <nameservers-lines>\n"
            "                  <_id>1</_id>\n"
            "                  <search-f>\n"
            "                    <search-list>\n"
            "                      <_seq>1</_seq>\n"
            "                      <scalar>lab</scalar>\n"
            "                    </search-list>\n"
            "                    <search-list>\n"
            "                      <_seq>2</_seq>\n"
            "                      <scalar>home</scalar>\n"
            "                    </search-list>\n"
            "                  </search-f>\n"
            "                </nameservers-lines>\n"
            "                <nameservers-lines>\n"
            "                  <_id>2</_id>\n"
            "                  <addresses-f>\n"
            "                    <addresses-list>\n"
            "                      <_seq>1</_seq>\n"
            "                      <scalar>8.8.8.8</scalar>\n"
            "                    </addresses-list>\n"
            "                    <addresses-list>\n"
            "                      <_seq>2</_seq>\n"
            "                      <scalar>\"FEDC::1\"</scalar>\n"
            "                    </addresses-list>\n"
            "                  </addresses-f>\n"
            "                </nameservers-lines>\n"
            "              </nameservers>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>9</_id>\n"
            "              <optional-addresses-f>\n"
            "                <optional-addresses-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <scalar>aa</scalar>\n"
            "                </optional-addresses-list>\n"
            "                <optional-addresses-list>\n"
            "                  <_seq>2</_seq>\n"
            "                  <scalar>rr d</scalar>\n"
            "                </optional-addresses-list>\n"
            "                <optional-addresses-list>\n"
            "                  <_seq>3</_seq>\n"
            "                  <scalar>\"gg\"</scalar>\n"
            "                </optional-addresses-list>\n"
            "              </optional-addresses-f>\n"
            "            </ethernet-opts>\n"
            "            <ethernet-opts>\n"
            "              <_id>10</_id>\n"
            "              <routes>\n"
            "                <routes-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <routes>\n"
            "                    <routes>1</routes>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <to>default</to>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>2</_id>\n"
            "                      <via>172.134.67.1</via>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>3</_id>\n"
            "                      <metric>100</metric>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>4</_id>\n"
            "                      <on-link>true</on-link>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>5</_id>\n"
            "                      <table>76</table>\n"
            "                    </routes-opts-lines>\n"
            "                  </routes>\n"
            "                </routes-list>\n"
            "                <routes-list>\n"
            "                  <_seq>2</_seq>\n"
            "                  <routes>\n"
            "                    <routes>1</routes>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <to>default</to>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>2</_id>\n"
            "                      <via>10.0.0.1</via>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>3</_id>\n"
            "                      <metric>100</metric>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>4</_id>\n"
            "                      <on-link>true</on-link>\n"
            "                    </routes-opts-lines>\n"
            "                  </routes>\n"
            "                </routes-list>\n"
            "              </routes>\n"
            "            </ethernet-opts>\n"
            "          </identifier>\n"
            "        </ethernets>\n"
            "        <ethernets>\n"
            "          <_id>2</_id>\n"
            "          <identifier>\n"
            "            <identifier>switchports</identifier>\n"
            "            <ethernet-opts>\n"
            "              <_id>1</_id>\n"
            "              <match-f>\n"
            "                <match-opts-flow>\n"
            "                  <_id>1</_id>\n"
            "                  <name>\"enp2*\"</name>\n"
            "                </match-opts-flow>\n"
            "              </match-f>\n"
            "            </ethernet-opts>\n"
            "          </identifier>\n"
            "        </ethernets>\n"
            "      </ethernets>\n"
            "    </config-entries>\n"
            "    <config-entries>\n"
            "      <_id>2</_id>\n"
            "      <bridges>\n"
            "        <bridges-list>\n"
            "          <_id>1</_id>\n"
            "          <bridges>\n"
            "            <identifier>br0</identifier>\n"
            "            <bridge-opts>\n"
            "              <_id>1</_id>\n"
            "              <interfaces-f>\n"
            "                <interfaces-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <scalar>switchports</scalar>\n"
            "                </interfaces-list>\n"
            "              </interfaces-f>\n"
            "            </bridge-opts>\n"
            "            <bridge-opts>\n"
            "              <_id>2</_id>\n"
            "              <parameters>\n"
            "                <bridge-parameters-lines>\n"
            "                  <_id>1</_id>\n"
            "                  <ageing-time>50</ageing-time>\n"
            "                </bridge-parameters-lines>\n"
            "                <bridge-parameters-lines>\n"
            "                  <_id>2</_id>\n"
            "                  <hello-time>10</hello-time>\n"
            "                </bridge-parameters-lines>\n"
            "              </parameters>\n"
            "            </bridge-opts>\n"
            "          </bridges>\n"
            "        </bridges-list>\n"
            "        <bridges-list>\n"
            "          <_id>2</_id>\n"
            "          <bridges>\n"
            "            <identifier>br1</identifier>\n"
            "            <bridge-opts>\n"
            "              <_id>1</_id>\n"
            "              <interfaces-f/>\n"
            "            </bridge-opts>\n"
            "          </bridges>\n"
            "        </bridges-list>\n"
            "      </bridges>\n"
            "    </config-entries>\n"
            "    <config-entries>\n"
            "      <_id>3</_id>\n"
            "      <tunnels>\n"
            "        <tunnels-list>\n"
            "          <_id>1</_id>\n"
            "          <tunnels>\n"
            "            <identifier>tn0</identifier>\n"
            "            <tunnel-opts>\n"
            "              <_id>1</_id>\n"
            "              <mode>vxlan</mode>\n"
            "            </tunnel-opts>\n"
            "            <tunnel-opts>\n"
            "              <_id>2</_id>\n"
            "              <keys>\n"
            "                <tunnel-keys-lines>\n"
            "                  <_id>1</_id>\n"
            "                  <input>1234</input>\n"
            "                </tunnel-keys-lines>\n"
            "                <tunnel-keys-lines>\n"
            "                  <_id>2</_id>\n"
            "                  <output>5678</output>\n"
            "                </tunnel-keys-lines>\n"
            "                <tunnel-keys-lines>\n"
            "                  <_id>3</_id>\n"
            "                  <private>/path/to/private.key</private>\n"
            "                </tunnel-keys-lines>\n"
            "              </keys>\n"
            "            </tunnel-opts>\n"
            "            <tunnel-opts>\n"
            "              <_id>3</_id>\n"
            "              <peers>\n"
            "                <peers-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <peers>\n"
            "                    <peers>1</peers>\n"
            "                    <config-entries>\n"
            "                      <_id>1</_id>\n"
            "                      <keys>\n"
            "                        <peer-keys-lines>\n"
            "                          <_id>1</_id>\n"
            "                          <public>rlbInAj0qV69CysWPQY7KEBnKxpYCpaWqOs/dLevdWc=</public>\n"
            "                        </peer-keys-lines>\n"
            "                      </keys>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>2</_id>\n"
            "                      <allowed-ips-f>\n"
            "                        <allowed-ips-list>\n"
            "                          <_seq>1</_seq>\n"
            "                          <scalar>0.0.0.0/0</scalar>\n"
            "                        </allowed-ips-list>\n"
            "                        <allowed-ips-list>\n"
            "                          <_seq>2</_seq>\n"
            "                          <scalar>\"2001:fe:ad:de:ad:be:ef:1/24\"</scalar>\n"
            "                        </allowed-ips-list>\n"
            "                      </allowed-ips-f>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>3</_id>\n"
            "                      <keepalive>23</keepalive>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>4</_id>\n"
            "                      <endpoint>1.2.3.4:5</endpoint>\n"
            "                    </config-entries>\n"
            "                  </peers>\n"
            "                </peers-list>\n"
            "                <peers-list>\n"
            "                  <_seq>2</_seq>\n"
            "                  <peers>\n"
            "                    <peers>1</peers>\n"
            "                    <config-entries>\n"
            "                      <_id>1</_id>\n"
            "                      <keys>\n"
            "                        <peer-keys-lines>\n"
            "                          <_id>1</_id>\n"
            "                          <public>M9nt4YujIOmNrRmpIRTmYSfMdrpvE7u6WkG8FY8WjG4=</public>\n"
            "                        </peer-keys-lines>\n"
            "                        <peer-keys-lines>\n"
            "                          <_id>2</_id>\n"
            "                          <shared>/some/shared.key</shared>\n"
            "                        </peer-keys-lines>\n"
            "                      </keys>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>2</_id>\n"
            "                      <allowed-ips-f>\n"
            "                        <allowed-ips-list>\n"
            "                          <_seq>1</_seq>\n"
            "                          <scalar>10.10.10.20/24</scalar>\n"
            "                        </allowed-ips-list>\n"
            "                      </allowed-ips-f>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>3</_id>\n"
            "                      <keepalive>22</keepalive>\n"
            "                    </config-entries>\n"
            "                    <config-entries>\n"
            "                      <_id>4</_id>\n"
            "                      <endpoint>5.4.3.2:1</endpoint>\n"
            "                    </config-entries>\n"
            "                  </peers>\n"
            "                </peers-list>\n"
            "              </peers>\n"
            "            </tunnel-opts>\n"
            "          </tunnels>\n"
            "        </tunnels-list>\n"
            "      </tunnels>\n"
            "    </config-entries>\n"
            "    <config-entries>\n"
            "      <_id>4</_id>\n"
            "      <vrfs>\n"
            "        <vrfs-list>\n"
            "          <_id>1</_id>\n"
            "          <vrfs>\n"
            "            <identifier>vrf20</identifier>\n"
            "            <vrf-opts>\n"
            "              <_id>1</_id>\n"
            "              <table>20</table>\n"
            "            </vrf-opts>\n"
            "            <vrf-opts>\n"
            "              <_id>2</_id>\n"
            "              <interfaces-f>\n"
            "                <interfaces-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <scalar>br0</scalar>\n"
            "                </interfaces-list>\n"
            "              </interfaces-f>\n"
            "            </vrf-opts>\n"
            "            <vrf-opts>\n"
            "              <_id>3</_id>\n"
            "              <routes>\n"
            "                <routes-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <routes>\n"
            "                    <routes>1</routes>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <to>default</to>\n"
            "                    </routes-opts-lines>\n"
            "                    <routes-opts-lines>\n"
            "                      <_id>2</_id>\n"
            "                      <via>10.10.10.3</via>\n"
            "                    </routes-opts-lines>\n"
            "                  </routes>\n"
            "                </routes-list>\n"
            "              </routes>\n"
            "            </vrf-opts>\n"
            "            <vrf-opts>\n"
            "              <_id>4</_id>\n"
            "              <routing-policy>\n"
            "                <routing-policy-list>\n"
            "                  <_seq>1</_seq>\n"
            "                  <routing-policy>\n"
            "                    <routing-policy>1</routing-policy>\n"
            "                    <routing-policy-opts-lines>\n"
            "                      <_id>1</_id>\n"
            "                      <from>10.10.10.42</from>\n"
            "                    </routing-policy-opts-lines>\n"
            "                  </routing-policy>\n"
            "                </routing-policy-list>\n"
            "              </routing-policy>\n"
            "            </vrf-opts>\n"
            "          </vrfs>\n"
            "        </vrfs-list>\n"
            "      </vrfs>\n"
            "    </config-entries>\n"
            "    <config-entries>\n"
            "      <_id>5</_id>\n"
            "      <version>2</version>\n"
            "    </config-entries>\n"
            "  </network>\n"
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

    /* add list instances */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='3']/identifier/identifier", "eth10", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='3']/identifier/ethernet-opts[_id='1']/addresses/addresses-list[_seq='1']/addresses-lines/"
            "identifier", "16", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='3']/identifier/ethernet-opts[_id='1']/addresses/addresses-list[_seq='1']/addresses-lines/"
            "config-entries[_id='1']/label", "lab", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='3']/identifier/ethernet-opts[_id='2']/receive-checksum-offload", "true", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='3']/identifier/ethernet-opts[_id='3']/transmit-checksum-offload", "true", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "network/config-entries[_id='1']/ethernets/ethernets[_id='1']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='6']/wifis/"
            "wifis-list[_id='1']/wifis/identifier", "wlo1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='6']/wifis/"
            "wifis-list[_id='1']/wifis/wifi-opts[_id='1']/match/match-opts-lines[_id='1']/driver/"
            "driver-list[_seq='1']/scalar", "scl", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "41a42,47\n"
            ">     eth10:\n"
            ">       addresses:\n"
            ">         - 16:\n"
            ">             label: lab\n"
            ">       receive-checksum-offload: true\n"
            ">       transmit-checksum-offload: true\n"
            "80a87,91\n"
            ">   wifis:\n"
            ">     wlo1:\n"
            ">       match:\n"
            ">         driver:\n"
            ">           - scl\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify leaves */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='1']/ethernets/"
            "ethernets[_id='1']/identifier/ethernet-opts[_id='5']/openvswitch/openvswitch-opts-lines[_id='2']/"
            "controller/controller-opts-lines[_id='1']/addresses-f/addresses-list[_seq='1']/scalar", "udp:127.0.0.1:6653",
            LYD_NEW_PATH_UPDATE, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "network/config-entries[_id='4']/vrfs/"
            "vrfs-list[_id='1']/vrfs/vrf-opts[_id='3']/routes/routes-list[_seq='1']/routes/routes-opts-lines[_id='2']/"
            "via", "10.10.10.1", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "16c16\n"
            "<           addresses: [ tcp:127.0.0.1:6653  , \"ssl:[fe80::1234%eth0]:6653\"]\n"
            "---\n"
            ">           addresses: [ udp:127.0.0.1:6653  , \"ssl:[fe80::1234%eth0]:6653\"]\n"
            "77c77\n"
            "<           via: 10.10.10.3\n"
            "---\n"
            ">           via: 10.10.10.1\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove nodes */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "network/config-entries[_id='3']/tunnels/tunnels-list[_id='1']/"
            "tunnels/tunnel-opts[_id='3']/peers/peers-list[_seq='2']/peers/config-entries[_id='3']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "network/config-entries[_id='4']/vrfs/vrfs-list[_id='1']/vrfs/"
            "vrf-opts[_id='3']/routes/routes-list[_seq='1']/routes/routes-opts-lines[_id='1']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, NULL, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "69d68\n"
            "<           keepalive: 22\n"
            "76,77c75\n"
            "<         - to: default\n"
            "<           via: 10.10.10.3\n"
            "---\n"
            ">         - via: 10.10.10.3\n"));
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
