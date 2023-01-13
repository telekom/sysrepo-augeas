(*
Module: Netplan
  Parses: /etc/netplan/*.yaml

Author: Michal Vasko <mvasko@cesnet.cz>
*)

module Netplan =
  autoload xfm

(************************************************************************
 * Group:                 USEFUL PRIMITIVES
 *************************************************************************)

let del_space = del /[ \t]*/ ""
let space = del /[ \t]+/ " "
let force_space = del / [ \t]*/ " "
let colon = Util.del_str ":"
let comment = [ label "#comment" . del /#[ \t]*/ "# " . store /([^ \t\n].*[^ \t\n]|[^ \t\n])/ ]
let nl = del_space . del /\n/ "\n"
let indent1 = "  "
let indent2 = "    "
let indent3 = "      "
let indent4 = "        "
let indent5 = "          "
let indent6 = "            "
let comma = Util.del_str ","
let dquote = Util.del_str "\""
let squote = Util.del_str "'"
let lbrack = Util.del_str "["
let rbrack = Util.del_str "]"
let lbrac = Util.del_str "{"
let rbrac = Util.del_str "}"
let dashed = del /- [ \t]*/ "- "

(************************************************************************
 * Group:                 TYPES
 *************************************************************************)

let uscalar_no_first = /[^]\t\r\n !"#%&'*,>@[`\{|\}]/
let identifier_no_first = /[^]\t\r\n !"%&'*:>@[`\{|\}-]/

(* no ',' '#' ']' '}' *)
let uscalar_content = uscalar_no_first . /[!"$-+.-\^-|~-]*/

(* no spaces, ':' *)
let uidentifier_content = identifier_no_first . /[!-9;-~]*/

let unquoted_scalar = uscalar_content . ( /[\t ]+/ . uscalar_content )*
let squoted_scalar = /'(''|[\t -&\(-~])*'/
let dquoted_scalar = /"([\]([0abtnvfre "\/\N_LP]|x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})|[]\t\r\n !#-[^-~])*"/

let scalar = unquoted_scalar | squoted_scalar | dquoted_scalar
let identifier = uidentifier_content | squoted_scalar | dquoted_scalar
let bool = /(true|false)/
let number = /[0-9]+/

let macaddress = /[0-9a-fA-F]{2}(((:[0-9a-fA-F]{2}){5})|((:[0-9a-fA-F]{2}){19}))/
let openvswitch_lacp = /active|passive|off/
let openvswitch_fail_mode = /secure|standalone/
let openvswitch_protocols = /OpenFlow1[0-6]/
let connection_mode = /in-band|out-of-band/
let renderer = /networkd|NetworkManager|sriov/
let link_local = /ipv4|ipv6/
let dhcp_identifier = /mac|duid/
let lifetime = /forever|0/
let ipv6_address_generation = /eui64|stable-privacy/
let route_type = /unicast|anycast|blackhole|broadcast|local|multicast|nat|prohibit|throw|unreachable|xresolve/
let route_scope = /global|link|host/
let activation_mode = /manual|off/
let key_management = /none|psk|eap|802\.1x/
let auth_method = /peap|t?tls/
let embedded_switch_mode = /switchdev|legacy/
let infiniband_mode = /datagram|connected/
let wifi_mode = /infrastructure|ap|adhoc/
let wifi_band = /5Ghz|2\.4Ghz/
let wakeonlan = /any|disconnect|magic_pkt|gtk_rekey_failure|eap_identity_req|four_way_handshake|rfkill_release|tcp|default/
let bond_mode = /balance-rr|active-backup|balance-xor|broadcast|802\.3ad|balance-tlb|balance-alb|active-backup
        |balance-tcp|balance-slb/
let lacp_rate = /slow|fast/
let transmit_hash_policy = /layer2|layer3\+4|layer2\+3|encap2\+3|encap3\+4/
let ad_select = /stable|bandwidth|count/
let arp_validate = /none|active|backup|all/
let arp_all_targets = /any|all/
let fail_over_mac_policy = /none|active|follow/
let primary_reselect_policy = /always|better|failure/
let tunnel_mode = /sit|gre|ip6gre|ipip|ipip6|ip6ip6|vti|vti6|wireguard|vxlan|gretap|ip6gretap|isatap/
let tunnel_port = /auto/ | number
let tunnel_notifications = /l[23]-miss/
let tunnel_checksum = /udp|zero-udp6-[tr]x|remote-[tr]x/
let tunnel_extension = /group-policy|generic-protocol/

(************************************************************************
 * Group:                 FUNCTIONS
 *************************************************************************)

(* View: nosubtree_mapping_lines *)
let nosubtree_mapping_lines (indt:string) (nested:lens) = nested
        . ( nl . del indt indt . nested )*

(* View: nosubtree_mapping_flow *)
let nosubtree_mapping_flow (nested:lens) = lbrac . del_space . nested . del_space
        . (comma . del_space . nested . del_space)* . rbrac

(* View: nosubtree_sequence_lines *)
let nosubtree_sequence_lines (indt:string) (nested:lens) = dashed . nested
        . ( nl . del indt indt . dashed . nested )*

(* View: nosubtree_sequence_flow *)
let nosubtree_sequence_flow (nested:lens) = lbrack . del_space . ( nested . del_space
        . (comma . del_space . nested . del_space)* )? . rbrack

(* View: subtree_key_inlines *)
let subtree_key_inlines (k:regexp) (nested:lens) = [ key k . colon . force_space . nested ]

(* View: subtree_key_inflow *)
let subtree_key_inflow (k:regexp) (nested:lens) = [ key k . colon . force_space . nested ]

(* View: subtree_key_mapping_lines *)
let subtree_key_mapping_lines (k:regexp) (indt:string) (nested:lens) =
        [ key k . colon . nl . del indt indt . (nosubtree_mapping_lines indt nested) ]

(* View: subtree_key_inlines_or_mapping_lines *)
let subtree_key_inlines_or_mapping_lines (k:regexp) (indt:string) (nested:lens) =
        [ key k . (colon . nl . del indt indt . (nosubtree_mapping_lines indt nested))? ]

(* View: subtree_key_mapping_flow2 *)
let subtree_key_mapping_flow2 (k:string) (nested:lens) = [ del k k . label (k . "_f") . colon
        . force_space . (nosubtree_mapping_flow nested) ]

(* View: subtree_key_sequence_lines *)
let subtree_key_sequence_lines (k:string) (indt:string) (nested:lens) = [ key k . colon . nl . del indt indt
        . counter k . nosubtree_sequence_lines indt [ seq k . nested ] ]

(* View: subtree_key_sequence_flow *)
let subtree_key_sequence_flow (k:string) (nested:lens) = [ key k . colon . force_space . counter k
        . nosubtree_sequence_flow [ seq k . nested ] ]

(* View: subtree_key_sequence_flow2 *)
let subtree_key_sequence_flow2 (k:string) (nested:lens) = [ del k k . label (k . "_f") . colon . force_space
        . counter k . nosubtree_sequence_flow [ seq k . nested ] ]

(* View: subtree_key_sequence *)
let subtree_key_sequence (k:string) (indt:string) (nested:lens) = (subtree_key_sequence_lines k indt nested)
        | [ del k k . label (k . "_f") . colon . force_space . (counter k . nosubtree_sequence_flow [ seq k . nested ]) ]

(* View: subtree_key_inlines_or_sequence_lines *)
let subtree_key_inlines_or_sequence_lines (k:string) (indt:string) (nested:lens) = del k k . colon .
        ( [ label (k . "_single") . force_space . nested ]
        | [ label k . counter k . nl . del indt indt . nosubtree_sequence_lines indt [ seq k . nested ] ] )

(* View: subtree_key_inflow_or_sequence_flow *)
let subtree_key_inflow_or_sequence_flow (k:string) (nested:lens) = del k k . colon .
        ( [ label (k . "_single") . force_space . nested ]
        | [ label k . counter k . force_space . nosubtree_sequence_flow [ seq k . nested ] ] )

(************************************************************************
 * Group:                 PHYSICAL DEVICE PROPERTIES
 *************************************************************************)

(* View: match_opts_lines *)
let match_opts_lines =
        subtree_key_inlines "name" (store scalar)
        | subtree_key_inlines "macaddress" (store macaddress)
        | subtree_key_inlines_or_sequence_lines "driver" indent5 (store scalar)

(* View: match_opts_flow *)
let match_opts_flow =
        subtree_key_inflow "name" (store scalar)
        | subtree_key_inflow "macaddress" (store macaddress)
        | subtree_key_inflow_or_sequence_flow "driver" (store scalar)

(* View: controller_opts_lines *)
let controller_opts_lines =
        subtree_key_sequence "addresses" indent6 (store scalar)
        | subtree_key_inlines "connection-mode" (store connection_mode)

(* View: controller_opts_flow *)
let controller_opts_flow =
        subtree_key_sequence_flow "addresses" (store scalar)
        | subtree_key_inflow "connection-mode" (store connection_mode)

(* View: ssl_opts_lines *)
let ssl_opts_lines =
        subtree_key_inlines "ca-cert" (store scalar)
        | subtree_key_inlines "certificate" (store scalar)
        | subtree_key_inlines "private-key" (store scalar)

(* View: ssl_opts_flow *)
let ssl_opts_flow =
        subtree_key_inflow "ca-cert" (store scalar)
        | subtree_key_inflow "certificate" (store scalar)
        | subtree_key_inflow "private-key" (store scalar)

(* View: openvswitch_opts_lines - "external-ids" and "other-config" not supported *)
let openvswitch_opts_lines =
        subtree_key_inlines "lacp" (store openvswitch_lacp)
        | subtree_key_inlines "fail-mode" (store openvswitch_fail_mode)
        | subtree_key_inlines "mcast-snooping" (store bool)
        | subtree_key_sequence "protocols" indent5 (store openvswitch_protocols)
        | subtree_key_inlines "rstp" (store bool)
        | subtree_key_mapping_lines "controller" indent5 controller_opts_lines
        | subtree_key_mapping_flow2 "controller" controller_opts_flow
        | subtree_key_sequence_lines "ports" indent5 (counter "ports" . nosubtree_sequence_flow [seq "ports" . (store scalar)])
        | subtree_key_mapping_lines "ssl" indent5 ssl_opts_lines
        | subtree_key_mapping_flow2 "ssl" ssl_opts_flow

(* View: physical_dev_props_lines *)
let physical_dev_props_lines =
        subtree_key_mapping_lines "match" indent4 match_opts_lines
        | subtree_key_mapping_flow2 "match" match_opts_flow
        | subtree_key_inlines "set-name" (store scalar)
        | subtree_key_inlines "wakeonlan" (store bool)
        | subtree_key_inlines "emit-lldp" (store bool)
        | subtree_key_inlines "receive-checksum-offload" (store bool)
        | subtree_key_inlines "transmit-checksum-offload" (store bool)
        | subtree_key_inlines "tcp-segmentation-offload" (store bool)
        | subtree_key_inlines "tcp6-segmentation-offload" (store bool)
        | subtree_key_inlines "generic-segmentation-offload" (store bool)
        | subtree_key_inlines "generic-receive-offload" (store bool)
        | subtree_key_inlines "large-receive-offload" (store bool)
        | subtree_key_mapping_lines "openvswitch" indent4 openvswitch_opts_lines

(************************************************************************
 * Group:                 COMMON DEVICE PROPERTIES AND AUTHENTICATION
 *************************************************************************)

(* View: dhcp_overrides_lines *)
let dhcp_overrides_lines =
        subtree_key_inlines "use-dns" (store bool)
        | subtree_key_inlines "use-ntp" (store bool)
        | subtree_key_inlines "send-hostname" (store bool)
        | subtree_key_inlines "use-hostname" (store bool)
        | subtree_key_inlines "use-mtu" (store bool)
        | subtree_key_inlines "hostname" (store scalar)
        | subtree_key_inlines "use-routes" (store bool)
        | subtree_key_inlines "route-metric" (store scalar)
        | subtree_key_inlines "use-domains" (store scalar)

(* View: dhcp_overrides_flow *)
let dhcp_overrides_flow =
        subtree_key_inflow "use-dns" (store bool)
        | subtree_key_inflow "use-ntp" (store bool)
        | subtree_key_inflow "send-hostname" (store bool)
        | subtree_key_inflow "use-hostname" (store bool)
        | subtree_key_inflow "use-mtu" (store bool)
        | subtree_key_inflow "hostname" (store scalar)
        | subtree_key_inflow "use-routes" (store bool)
        | subtree_key_inflow "route-metric" (store scalar)
        | subtree_key_inflow "use-domains" (store scalar)

(* View: addresses_lines *)
let addresses_lines = subtree_key_inlines_or_mapping_lines identifier indent6 (
        subtree_key_inlines "lifetime" (store lifetime)
        | subtree_key_inlines "label" (store scalar) )

(* View: nameservers_lines *)
let nameservers_lines =
        subtree_key_sequence "addresses" indent5 (store scalar)
        | subtree_key_sequence "search" indent5 (store scalar)

(* View: nameservers_flow *)
let nameservers_flow =
        subtree_key_sequence_flow "addresses" (store scalar)
        | subtree_key_sequence_flow "search" (store scalar)

(* View: routes_opts_lines *)
let routes_opts_lines =
        subtree_key_inlines "from" (store scalar)
        | subtree_key_inlines "to" (store scalar)
        | subtree_key_inlines "via" (store scalar)
        | subtree_key_inlines "on-link" (store bool)
        | subtree_key_inlines "metric" (store number)
        | subtree_key_inlines "type" (store route_type)
        | subtree_key_inlines "scope" (store route_scope)
        | subtree_key_inlines "table" (store number)
        | subtree_key_inlines "mtu" (store number)
        | subtree_key_inlines "congestion-window" (store number)
        | subtree_key_inlines "advertised-receive-window" (store number)

(* View: routing_policy_opts_lines *)
let routing_policy_opts_lines =
        subtree_key_inlines "from" (store scalar)
        | subtree_key_inlines "to" (store scalar)
        | subtree_key_inlines "table" (store number)
        | subtree_key_inlines "priority" (store number)
        | subtree_key_inlines "mark" (store number)
        | subtree_key_inlines "type-of-service" (store scalar)

(* View: common_dev_props_lines *)
let common_dev_props_lines =
        subtree_key_inlines "renderer" (store renderer)
        | subtree_key_inlines "dhcp4" (store bool)
        | subtree_key_inlines "dhcp6" (store bool)
        | subtree_key_inlines "ipv6-mtu" (store number)
        | subtree_key_inlines "ipv6-privacy" (store bool)
        | subtree_key_sequence "link-local" indent4 (store link_local)
        | subtree_key_inlines "ignore-carrier" (store bool)
        | subtree_key_inlines "critical" (store bool)
        | subtree_key_inlines "dhcp-identifier" (store dhcp_identifier)
        | subtree_key_mapping_lines "dhcp4-overrides" indent4 dhcp_overrides_lines
        | subtree_key_mapping_flow2 "dhcp4-overrides" dhcp_overrides_flow
        | subtree_key_mapping_lines "dhcp6-overrides" indent4 dhcp_overrides_lines
        | subtree_key_mapping_flow2 "dhcp6-overrides" dhcp_overrides_flow
        | subtree_key_inlines "accept-ra" (store bool)
        | subtree_key_sequence_lines "addresses" indent4 addresses_lines
        | subtree_key_inlines "ipv6-address-generation" (store ipv6_address_generation)
        | subtree_key_inlines "ipv6-address-token" (store scalar)
        | subtree_key_inlines "gateway4" (store scalar)
        | subtree_key_inlines "gateway6" (store scalar)
        | subtree_key_mapping_lines "nameservers" indent4 nameservers_lines
        | subtree_key_mapping_flow2 "nameservers" nameservers_flow
        | subtree_key_inlines "macaddress" (store macaddress)
        | subtree_key_inlines "mtu" (store number)
        | subtree_key_inlines "optional" (store bool)
        | subtree_key_sequence "optional-addresses" indent4 (store scalar)
        | subtree_key_inlines "activation-mode" (store activation_mode)
        | subtree_key_sequence_lines "routes" indent4 (
        counter "routes" . [seq "routes" . nosubtree_mapping_lines indent5 routes_opts_lines] )
        | subtree_key_sequence_lines "routing-policy" indent4 (
        counter "routing-policy" . [seq "routing-policy" . nosubtree_mapping_lines indent5 routing_policy_opts_lines] )
        | subtree_key_inlines "neigh-suppress" (store scalar)

(* View: auth_opts_lines *)
let auth_opts_lines =
        subtree_key_inlines "key-management" (store key_management)
        | subtree_key_inlines "password" (store scalar)
        | subtree_key_inlines "method" (store auth_method)
        | subtree_key_inlines "identity" (store scalar)
        | subtree_key_inlines "anonymous-identity" (store scalar)
        | subtree_key_inlines "ca-certificate" (store scalar)
        | subtree_key_inlines "client-certificate" (store scalar)
        | subtree_key_inlines "client-key" (store scalar)
        | subtree_key_inlines "client-key-password" (store scalar)
        | subtree_key_inlines "phase2-auth" (store scalar)

(* View: auth_opts_flow *)
let auth_opts_flow =
        subtree_key_inflow "key-management" (store key_management)
        | subtree_key_inflow "password" (store scalar)
        | subtree_key_inflow "method" (store auth_method)
        | subtree_key_inflow "identity" (store scalar)
        | subtree_key_inflow "anonymous-identity" (store scalar)
        | subtree_key_inflow "ca-certificate" (store scalar)
        | subtree_key_inflow "client-certificate" (store scalar)
        | subtree_key_inflow "client-key" (store scalar)
        | subtree_key_inflow "client-key-password" (store scalar)
        | subtree_key_inflow "phase2-auth" (store scalar)

(************************************************************************
 * Group:                 ETHERNETS
 *************************************************************************)

(* View: ethernet_opts *)
let ethernet_opts = physical_dev_props_lines | common_dev_props_lines
        | subtree_key_mapping_lines "auth" indent4 auth_opts_lines
        | subtree_key_mapping_flow2 "auth" auth_opts_flow
        | subtree_key_inlines "link" (store scalar)
        | subtree_key_inlines "virtual-function-count" (store number)
        | subtree_key_inlines "embedded-switch-mode" (store embedded_switch_mode)
        | subtree_key_inlines "delay-virtual-functions-rebind" (store bool)
        | subtree_key_inlines "infiniband-mode" (store infiniband_mode)

(* View: ethernets *)
let ethernets =
        subtree_key_inlines "renderer" (store renderer)
        | subtree_key_mapping_lines identifier indent3 ethernet_opts

(************************************************************************
 * Group:                 MODEMS
 *************************************************************************)

(* View: modem_opts *)
let modem_opts = physical_dev_props_lines | common_dev_props_lines
        | subtree_key_inlines "apn" (store scalar)
        | subtree_key_inlines "auto-config" (store bool)
        | subtree_key_inlines "device-id" (store scalar)
        | subtree_key_inlines "network-id" (store scalar)
        | subtree_key_inlines "number" (store scalar)
        | subtree_key_inlines "password" (store scalar)
        | subtree_key_inlines "pin" (store scalar)
        | subtree_key_inlines "sim-id" (store scalar)
        | subtree_key_inlines "sim-operator-id" (store scalar)
        | subtree_key_inlines "username" (store scalar)

(* View: modems *)
let modems = subtree_key_mapping_lines identifier indent3 modem_opts

(************************************************************************
 * Group:                 WIFIS
 *************************************************************************)

(* View: access_points_lines *)
let access_points_lines =
        subtree_key_inlines "password" (store scalar)
        | subtree_key_inlines "mode" (store wifi_mode)
        | subtree_key_inlines "bssid" (store scalar)
        | subtree_key_inlines "band" (store wifi_band)
        | subtree_key_inlines "channel" (store number)
        | subtree_key_inlines "hidden" (store bool)

(* View: wifi_opts *)
let wifi_opts = physical_dev_props_lines | common_dev_props_lines
        | subtree_key_mapping_lines "access-points" indent4 access_points_lines
        | subtree_key_sequence "wakeonlan" indent4 (store wakeonlan)
        | subtree_key_inlines "regulatory-domain" (store scalar)

(* View: wifis *)
let wifis = subtree_key_mapping_lines identifier indent3 wifi_opts

(************************************************************************
 * Group:                 BRIDGES
 *************************************************************************)

(* View: bridge_parameters_lines *)
let bridge_parameters_lines =
        subtree_key_inlines /age?ing-time/ (store number)
        | subtree_key_inlines "priority" (store number)
        | subtree_key_inlines "port-priority" (store number)
        | subtree_key_inlines "forward-delay" (store number)
        | subtree_key_inlines "hello-time" (store number)
        | subtree_key_inlines "max-age" (store number)
        | subtree_key_inlines "path-cost" (store scalar)
        | subtree_key_inlines "stp" (store bool)

(* View: bridge_opts *)
let bridge_opts = common_dev_props_lines
        | subtree_key_sequence "interfaces" indent4 (store scalar)
        | subtree_key_mapping_lines "parameters" indent4 bridge_parameters_lines

(* View: bridges *)
let bridges = subtree_key_mapping_lines identifier indent3 bridge_opts

(************************************************************************
 * Group:                 BONDS
 *************************************************************************)

(* View: bond_parameters *)
let bond_parameters =
        subtree_key_inlines "mode" (store bond_mode)
        | subtree_key_inlines "lacp-rate" (store lacp_rate)
        | subtree_key_inlines "mii-monitor-interval" (store number)
        | subtree_key_inlines "min-links" (store number)
        | subtree_key_inlines "transmit-hash-policy" (store transmit_hash_policy)
        | subtree_key_inlines "ad-select" (store ad_select)
        | subtree_key_inlines "all-slaves-active" (store bool)
        | subtree_key_inlines "arp-interval" (store number)
        | subtree_key_sequence "arp-ip-targets" indent5 (store scalar)
        | subtree_key_inlines "arp-validate" (store arp_validate)
        | subtree_key_inlines "arp-all-targets" (store arp_all_targets)
        | subtree_key_inlines "up-delay" (store number)
        | subtree_key_inlines "down-delay" (store number)
        | subtree_key_inlines "fail-over-mac-policy" (store fail_over_mac_policy)
        | subtree_key_inlines "gratuitous-arp" (store number)
        | subtree_key_inlines "packets-per-slave" (store number)
        | subtree_key_inlines "primary-reselect-policy" (store primary_reselect_policy)
        | subtree_key_inlines "resend-igmp" (store number)
        | subtree_key_inlines "learn-packet-interval" (store scalar)
        | subtree_key_inlines "primary" (store scalar)

(* View: bond_opts *)
let bond_opts = common_dev_props_lines
        | subtree_key_sequence "interfaces" indent4 (store scalar)
        | subtree_key_mapping_lines "parameters" indent4 bond_parameters

(* View: bonds *)
let bonds = subtree_key_mapping_lines identifier indent3 bond_opts

(************************************************************************
 * Group:                 TUNNELS
 *************************************************************************)

(* View: tunnel_keys_lines *)
let tunnel_keys_lines =
        subtree_key_inlines "input" (store scalar)
        | subtree_key_inlines "output" (store scalar)
        | subtree_key_inlines "private" (store scalar)

(* View: tunnel_keys_flow *)
let tunnel_keys_flow =
        subtree_key_inflow "input" (store scalar)
        | subtree_key_inflow "output" (store scalar)
        | subtree_key_inflow "private" (store scalar)

(* View: peer_keys_lines *)
let peer_keys_lines =
        subtree_key_inlines "public" (store scalar)
        | subtree_key_inlines "shared" (store scalar)

(* View: peer_keys_flow *)
let peer_keys_flow =
        subtree_key_inflow "public" (store scalar)
        | subtree_key_inflow "shared" (store scalar)

(* View: peers_lines *)
let peers_lines = counter "peers" . [seq "peers" . nosubtree_mapping_lines indent5
        ( subtree_key_inlines "endpoint" (store scalar)
        | subtree_key_sequence "allowed-ips" indent6 (store scalar)
        | subtree_key_inlines "keepalive" (store number)
        | subtree_key_mapping_lines "keys" indent6 peer_keys_lines
        | subtree_key_mapping_flow2 "keys" peer_keys_flow )]

(* View: tunnel_opts *)
let tunnel_opts = common_dev_props_lines
        | subtree_key_inlines "mode" (store tunnel_mode)
        | subtree_key_inlines "local" (store scalar)
        | subtree_key_inlines "remote" (store scalar)
        | subtree_key_inlines "ttl" (store number)
        | subtree_key_inlines "key" (store scalar)
        | subtree_key_mapping_lines "keys" indent4 tunnel_keys_lines
        | subtree_key_mapping_flow2 "keys" tunnel_keys_flow
        | subtree_key_inlines "mark" (store scalar)
        | subtree_key_inlines "port" (store tunnel_port)
        | subtree_key_sequence_lines "peers" indent4 peers_lines
        | subtree_key_inlines "id" (store number)
        | subtree_key_inlines "link" (store scalar)
        | subtree_key_inlines "type-of-service" (store scalar)
        | subtree_key_inlines "mac-learning" (store bool)
        | subtree_key_inlines /age?ing/ (store number)
        | subtree_key_inlines "limit" (store number)
        | subtree_key_inlines "arp-proxy" (store bool)
        | subtree_key_sequence "notifications" indent4 (store tunnel_notifications)
        | subtree_key_inlines "short-circuit" (store bool)
        | subtree_key_sequence "checksums" indent4 (store tunnel_checksum)
        | subtree_key_sequence "extensions" indent4 (store tunnel_extension)
        | subtree_key_sequence "port-range" indent4 (store number)
        | subtree_key_inlines "flow-label" (store number)
        | subtree_key_inlines "do-not-fragment" (store bool)

(* View: tunnels *)
let tunnels = subtree_key_mapping_lines identifier indent3 tunnel_opts

(************************************************************************
 * Group:                 VLANS
 *************************************************************************)

(* View: vlan_opts *)
let vlan_opts = common_dev_props_lines
        | subtree_key_inlines "id" (store number)
        | subtree_key_inlines "link" (store scalar)

(* View: vlans *)
let vlans = subtree_key_mapping_lines identifier indent3 vlan_opts

(************************************************************************
 * Group:                 VRFS
 *************************************************************************)

(* View: vrf_opts *)
let vrf_opts = common_dev_props_lines
        | subtree_key_inlines "table" (store number)
        | subtree_key_sequence "interfaces" indent4 (store scalar)

(* View: vrfs *)
let vrfs = subtree_key_mapping_lines identifier indent3 vrf_opts

(************************************************************************
 * Group:                 NETWORK MANAGER
 *************************************************************************)

(* View: network_manager_opts - "passthrough" not supported *)
let network_manager_opts =
        subtree_key_inlines "name" (store scalar)
        | subtree_key_inlines "uuid" (store scalar)
        | subtree_key_inlines "stable-id" (store scalar)
        | subtree_key_inlines "device" (store scalar)

(* View: network_manager *)
let network_manager = subtree_key_mapping_lines identifier indent3 network_manager_opts

(************************************************************************
 * Group:                 NETWORK
 *************************************************************************)

(* View: network *)
let network = subtree_key_mapping_lines "network" indent1
        ( subtree_key_inlines "version" (store /(1|2)/)
        | subtree_key_mapping_lines "ethernets" indent2 ethernets
        | subtree_key_mapping_lines "modems" indent2 modems
        | subtree_key_mapping_lines "wifis" indent2 wifis
        | subtree_key_mapping_lines "bridges" indent2 bridges
        | subtree_key_mapping_lines "bonds" indent2 bonds
        | subtree_key_mapping_lines "tunnels" indent2 tunnels
        | subtree_key_mapping_lines "vlans" indent2 vlans
        | subtree_key_mapping_lines "vrfs" indent2 vrfs
        | subtree_key_mapping_lines "network-manager" indent2 network_manager )

(* View: header *)
let header = [ label "@yaml" . Util.del_str "---"
        . ( Sep.space . store Rx.space_in )? . Util.eol ]

(*
 * View: lns
 *   The netplan lens
 *)
let lns = (( Util.empty | Util.comment_noindent )* . header)? . network . nl

(* Variable: filter *)
let filter = incl "/etc/netplan/*.yaml"
        . Util.stdexcl

let xfm = transform lns filter
