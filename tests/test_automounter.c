/**
 * @file test_automounter.c
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief automounter SR DS plugin test
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
#define AUG_TEST_INPUT_FILES AUG_CONFIG_FILES_DIR "/automounter"
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

#define AUG_TEST_MODULE "automounter"

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
            "  <entry-list>\n"
            "    <_id>1</_id>\n"
            "    <entry>\n"
            "      <entry>1</entry>\n"
            "      <entry-mkey>cd</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>fstype</optlabel>\n"
            "          <value>iso9660</value>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>ro</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>nosuid</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>4</_id>\n"
            "        <opt>\n"
            "          <optlabel>nodev</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <path>/dev/cdrom</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>2</_id>\n"
            "    <entry>\n"
            "      <entry>2</entry>\n"
            "      <entry-mkey>kernel</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>ro</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>soft</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>intr</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>ftp.kernel.org</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/pub/linux</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>3</_id>\n"
            "    <entry>\n"
            "      <entry>3</entry>\n"
            "      <entry-mkey>*</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>fstype</optlabel>\n"
            "          <value>auto</value>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>loop</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>ro</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <path>/srv/distros/isos/&amp;.iso</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>4</_id>\n"
            "    <entry>\n"
            "      <entry>4</entry>\n"
            "      <entry-mkey>/nfs/apps/mozilla</entry-mkey>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>bogus</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/usr/local/moxill</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>5</_id>\n"
            "    <entry>\n"
            "      <entry>5</entry>\n"
            "      <entry-mkey>path</entry-mkey>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host1</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <entry-host-list>\n"
            "              <_id>2</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host2</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <entry-host-list>\n"
            "              <_id>3</_id>\n"
            "              <entry-host>\n"
            "                <hostname>hostn</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/path/path</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>6</_id>\n"
            "    <entry>\n"
            "      <entry>6</entry>\n"
            "      <entry-mkey>path</entry-mkey>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host1</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <entry-host-list>\n"
            "              <_id>2</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host2</hostname>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/blah</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "        <location-list>\n"
            "          <_id>2</_id>\n"
            "          <location>\n"
            "            <location>2</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host3</hostname>\n"
            "                <weight>1</weight>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/some/other/path</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>7</_id>\n"
            "    <entry>\n"
            "      <entry>7</entry>\n"
            "      <entry-mkey>path</entry-mkey>\n"
            "      <entry-locations>\n"
            "        <location-list>\n"
            "          <_id>1</_id>\n"
            "          <location>\n"
            "            <location>1</location>\n"
            "            <entry-host-list>\n"
            "              <_id>1</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host1</hostname>\n"
            "                <weight>5</weight>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <entry-host-list>\n"
            "              <_id>2</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host2</hostname>\n"
            "                <weight>6</weight>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <entry-host-list>\n"
            "              <_id>3</_id>\n"
            "              <entry-host>\n"
            "                <hostname>host3</hostname>\n"
            "                <weight>1</weight>\n"
            "              </entry-host>\n"
            "            </entry-host-list>\n"
            "            <path>/path/path</path>\n"
            "          </location>\n"
            "        </location-list>\n"
            "      </entry-locations>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>8</_id>\n"
            "    <entry>\n"
            "      <entry>8</entry>\n"
            "      <entry-mkey>server</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>rw</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>hard</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>intr</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-multimounts>\n"
            "        <mount-list>\n"
            "          <_id>1</_id>\n"
            "          <mount>\n"
            "            <mount>1</mount>\n"
            "            <entry-mkey>/</entry-mkey>\n"
            "            <opt-list>\n"
            "              <_id>1</_id>\n"
            "              <opt>\n"
            "                <optlabel>ro</optlabel>\n"
            "              </opt>\n"
            "            </opt-list>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "      </entry-multimounts>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>9</_id>\n"
            "    <entry>\n"
            "      <entry>9</entry>\n"
            "      <entry-mkey>server</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>rw</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>hard</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>intr</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-multimounts>\n"
            "        <mount-list>\n"
            "          <_id>1</_id>\n"
            "          <mount>\n"
            "            <mount>1</mount>\n"
            "            <entry-mkey>/</entry-mkey>\n"
            "            <opt-list>\n"
            "              <_id>1</_id>\n"
            "              <opt>\n"
            "                <optlabel>ro</optlabel>\n"
            "              </opt>\n"
            "            </opt-list>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "        <mount-list>\n"
            "          <_id>2</_id>\n"
            "          <mount>\n"
            "            <mount>2</mount>\n"
            "            <entry-mkey>/usr</entry-mkey>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/usr</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "      </entry-multimounts>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>10</_id>\n"
            "    <entry>\n"
            "      <entry>10</entry>\n"
            "      <entry-mkey>server</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>rw</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>hard</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>intr</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-multimounts>\n"
            "        <mount-list>\n"
            "          <_id>1</_id>\n"
            "          <mount>\n"
            "            <mount>1</mount>\n"
            "            <entry-mkey>/</entry-mkey>\n"
            "            <opt-list>\n"
            "              <_id>1</_id>\n"
            "              <opt>\n"
            "                <optlabel>ro</optlabel>\n"
            "              </opt>\n"
            "            </opt-list>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "        <mount-list>\n"
            "          <_id>2</_id>\n"
            "          <mount>\n"
            "            <mount>2</mount>\n"
            "            <entry-mkey>/usr</entry-mkey>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/usr</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "        <mount-list>\n"
            "          <_id>3</_id>\n"
            "          <mount>\n"
            "            <mount>3</mount>\n"
            "            <entry-mkey>/home</entry-mkey>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>myserver.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/home</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "      </entry-multimounts>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>11</_id>\n"
            "    <entry>\n"
            "      <entry>11</entry>\n"
            "      <entry-mkey>server</entry-mkey>\n"
            "      <opt-list>\n"
            "        <_id>1</_id>\n"
            "        <opt>\n"
            "          <optlabel>rw</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>2</_id>\n"
            "        <opt>\n"
            "          <optlabel>hard</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <opt-list>\n"
            "        <_id>3</_id>\n"
            "        <opt>\n"
            "          <optlabel>intr</optlabel>\n"
            "        </opt>\n"
            "      </opt-list>\n"
            "      <entry-multimounts>\n"
            "        <mount-list>\n"
            "          <_id>1</_id>\n"
            "          <mount>\n"
            "            <mount>1</mount>\n"
            "            <entry-mkey>/</entry-mkey>\n"
            "            <opt-list>\n"
            "              <_id>1</_id>\n"
            "              <opt>\n"
            "                <optlabel>ro</optlabel>\n"
            "              </opt>\n"
            "            </opt-list>\n"
            "            <entry-locations>\n"
            "              <location-list>\n"
            "                <_id>1</_id>\n"
            "                <location>\n"
            "                  <location>1</location>\n"
            "                  <entry-host-list>\n"
            "                    <_id>1</_id>\n"
            "                    <entry-host>\n"
            "                      <hostname>my-with-dash-server.me.org</hostname>\n"
            "                    </entry-host>\n"
            "                  </entry-host-list>\n"
            "                  <path>/</path>\n"
            "                </location>\n"
            "              </location-list>\n"
            "            </entry-locations>\n"
            "          </mount>\n"
            "        </mount-list>\n"
            "      </entry-multimounts>\n"
            "    </entry>\n"
            "  </entry-list>\n"
            "  <entry-list>\n"
            "    <_id>12</_id>\n"
            "    <entry>\n"
            "      <entry>12</entry>\n"
            "      <entry-mkey>+</entry-mkey>\n"
            "      <map>auto_home</map>\n"
            "    </entry>\n"
            "  </entry-list>\n"
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
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry", "13", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-mkey", "server", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/opt-list[_id='1']/opt/optlabel",
            "var", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/opt-list[_id='1']/opt/value",
            "25", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/mount", "1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-mkey", "/", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-locations/location-list[_id='1']/location/location", "1", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-locations/location-list[_id='1']/location/entry-host-list[_id='1']/entry-host/"
            "hostname", "server.example.eu", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-locations/location-list[_id='1']/location/entry-host-list[_id='1']/entry-host/"
            "weight", "256", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='13']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-locations/location-list[_id='1']/location/path", "/usr/local", 0, NULL));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='3']/entry/opt-list[_id='5']/opt/optlabel",
            "option", 0, &entries));
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='3']/entry/opt-list[_id='2']", 0, &node));
    assert_int_equal(LY_SUCCESS, lyd_insert_after(node, entries));

    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='10']/entry/entry-multimounts/"
            "mount-list[_id='3']/mount/entry-locations/location-list[_id='2']/location/location", "2", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='10']/entry/entry-multimounts/"
            "mount-list[_id='3']/mount/entry-locations/location-list[_id='2']/location/entry-host-list[_id='1']/entry-host/"
            "hostname", "my-server.company.eu", 0, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='10']/entry/entry-multimounts/"
            "mount-list[_id='3']/mount/entry-locations/location-list[_id='2']/location/path", "/", 0, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "9c9\n"
            "< *       -fstype=auto,loop,ro    :/srv/distros/isos/&.iso\n"
            "---\n"
            "> *       -fstype=auto,loop,option,ro    :/srv/distros/isos/&.iso\n"
            "24c24\n"
            "<                               /home myserver.me.org:/home\n"
            "---\n"
            ">                               /home myserver.me.org:/home my-server.company.eu:/\n"
            "29a30\n"
            "> server\t-var=25\t/\tserver.example.eu(256):/usr/local\n"));
}

static void
test_store_modify(void **state)
{
    struct tstate *st = (struct tstate *)*state;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* modify some values */
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='8']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-mkey", "/root", LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='12']/entry/map", "auto_root",
            LYD_NEW_PATH_UPDATE, NULL));
    assert_int_equal(LY_SUCCESS, lyd_new_path(st->data, NULL, "entry-list[_id='11']/entry/entry-multimounts/"
            "mount-list[_id='1']/mount/entry-locations/location-list[_id='1']/location/entry-host-list[_id='1']/entry-host/"
            "hostname", "my-with-dash-server.me.net", LYD_NEW_PATH_UPDATE, NULL));

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "20c20\n"
            "< server    -rw,hard,intr       / -ro myserver.me.org:/\n"
            "---\n"
            "> server    -rw,hard,intr       /root -ro myserver.me.org:/\n"
            "26c26\n"
            "< server    -rw,hard,intr       / -ro my-with-dash-server.me.org:/\n"
            "---\n"
            "> server    -rw,hard,intr       / -ro my-with-dash-server.me.net:/\n"
            "29c29\n"
            "< +auto_home\n"
            "---\n"
            "> +auto_root\n"));
}

static void
test_store_remove(void **state)
{
    struct tstate *st = (struct tstate *)*state;
    struct lyd_node *node;

    /* load current data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->load_cb(st->mod, SR_DS_STARTUP, NULL, 0, &st->data));

    /* remove list values */
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='11']/entry/opt-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='10']/entry/entry-multimounts/"
            "mount-list[_id='2']", 0, &node));
    lyd_free_tree(node);
    assert_int_equal(LY_SUCCESS, lyd_find_path(st->data, "entry-list[_id='6']", 0, &node));
    lyd_free_tree(node);

    /* store new data */
    assert_int_equal(SR_ERR_OK, st->ds_plg->store_cb(st->mod, SR_DS_STARTUP, st->data));

    /* diff */
    assert_int_equal(0, tdiff_files(state,
            "16d15\n"
            "< path    host1,host2:/blah host3(1):/some/other/path\n"
            "23d21\n"
            "<                               /usr myserver.me.org:/usr \\\n"
            "26c24\n"
            "< server    -rw,hard,intr       / -ro my-with-dash-server.me.org:/\n"
            "---\n"
            "> server    -rw,intr       / -ro my-with-dash-server.me.org:/\n"));
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
