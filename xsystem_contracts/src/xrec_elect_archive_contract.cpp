// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xvm/xsystem_contracts/xelection/xrec/xrec_elect_archive_contract.h"

#include "xbasic/xutility.h"
#include "xcodec/xmsgpack_codec.hpp"
#include "xcommon/xnode_id.h"
#include "xconfig/xconfig_register.h"
#include "xdata/xcodec/xmsgpack/xelection_result_store_codec.hpp"
#include "xdata/xcodec/xmsgpack/xstandby_node_info_codec.hpp"
#include "xdata/xcodec/xmsgpack/xstandby_result_store_codec.hpp"
#include "xdata/xelection/xstandby_node_info.h"
#include "xdata/xelection/xelection_result_property.h"
#include "xdata/xgenesis_data.h"
#include "xconfig/xpredefined_configurations.h"
#include "xstake/xstake_algorithm.h"
#include "xvm/xserialization/xserialization.h"

#include <inttypes.h>

#ifndef XSYSCONTRACT_MODULE
#    define XSYSCONTRACT_MODULE "sysContract_"
#endif
#define XCONTRACT_PREFIX "RecElectArchive_"
#define XARCHIVE_ELECT XSYSCONTRACT_MODULE XCONTRACT_PREFIX

NS_BEG4(top, xvm, system_contracts, rec)

using common::xnode_id_t;
using data::election::xelection_result_store_t;
using data::election::xstandby_node_info_t;
using data::election::xstandby_result_store_t;

xtop_rec_elect_archive_contract::xtop_rec_elect_archive_contract(common::xnetwork_id_t const & network_id) : xbase_t{network_id} {}

#ifdef STATIC_CONSENSUS
bool executed_archive{false};
// if enabled static_consensus
// make sure add config in config.xxxx.json
// like this :
//
// "archive_start_nodes":"T00000LNi53Ub726HcPXZfC4z6zLgTo5ks6GzTUp.0.pub_key,T00000LeXNqW7mCCoj23LEsxEmNcWKs8m6kJH446.0.pub_key,T00000LVpL9XRtVdU5RwfnmrCtJhvQFxJ8TB46gB.0.pub_key",
//
// it will elect the first and only round archive nodes as you want.
static uint64_t node_start_time{UINT64_MAX};
void xtop_rec_elect_archive_contract::elect_config_nodes(common::xlogic_time_t const current_time) {
    uint64_t latest_height = get_blockchain_height(sys_contract_rec_elect_archive_addr);
    xinfo("[archive_start_nodes] get_latest_height: %" PRIu64, latest_height);
    if (latest_height > 0) {
        executed_archive = true;
        return;
    }

    using top::data::election::xelection_info_bundle_t;
    using top::data::election::xelection_info_t;
    using top::data::election::xelection_result_store_t;
    using top::data::election::xstandby_node_info_t;

    auto & config_register = top::config::xconfig_register_t::get_instance();
    std::string consensus_infos;
    config_register.get(std::string("archive_start_nodes"), consensus_infos);
    xinfo("[archive_start_nodes] read_all :%s", consensus_infos.c_str());
    std::vector<std::string> nodes_info;
    top::base::xstring_utl::split_string(consensus_infos, ',', nodes_info);

    auto property_names = data::election::get_property_name_by_addr(SELF_ADDRESS());
    auto election_result_store =
        xvm::serialization::xmsgpack_t<xelection_result_store_t>::deserialize_from_string_prop(*this, data::election::get_property_by_group_id(common::xdefault_group_id));
    auto node_type = common::xnode_type_t::archive;
    auto & election_group_result = election_result_store.result_of(network_id()).result_of(node_type).result_of(common::xdefault_cluster_id).result_of(common::xdefault_group_id);

    for (auto nodes : nodes_info) {
        xinfo("[archive_start_nodes] read :%s", nodes.c_str());
        std::vector<std::string> one_node_info;
        top::base::xstring_utl::split_string(nodes, '.', one_node_info);
        xelection_info_t new_election_info{};
        new_election_info.consensus_public_key = xpublic_key_t{one_node_info[2]};
        new_election_info.stake = static_cast<std::uint64_t>(std::atoi(one_node_info[1].c_str()));

        xelection_info_bundle_t election_info_bundle{};
        election_info_bundle.node_id(common::xnode_id_t{one_node_info[0]});
        election_info_bundle.election_info(std::move(new_election_info));

        election_group_result.insert(std::move(election_info_bundle));
    }

    election_group_result.election_committee_version(common::xversion_t{0});
    election_group_result.timestamp(current_time);
    election_group_result.start_time(current_time);
    if (election_group_result.group_version().empty()) {
        election_group_result.group_version(common::xversion_t::max());
    }
    xvm::serialization::xmsgpack_t<xelection_result_store_t>::serialize_to_string_prop(
        *this, data::election::get_property_by_group_id(common::xdefault_group_id), election_result_store);
}
#endif

void xtop_rec_elect_archive_contract::setup() {
    xelection_result_store_t election_result_store;
#ifdef STATIC_CONSENSUS
    node_start_time = base::xtime_utl::gmttime_ms();
#endif
    auto property_names = data::election::get_property_name_by_addr(SELF_ADDRESS());
    for (auto const & property : property_names) {
        STRING_CREATE(property);
        serialization::xmsgpack_t<xelection_result_store_t>::serialize_to_string_prop(*this, property, election_result_store);
    }
}

void xtop_rec_elect_archive_contract::on_timer(const uint64_t current_time) {
#ifdef STATIC_CONSENSUS
    auto current_gmt_time = base::xtime_utl::gmttime_ms();
    xinfo("[STATIC_CONSENSUS] node start gmt time: % " PRIu64 " current time % " PRIu64, node_start_time, current_gmt_time);
    uint64_t set_waste_time{20};
    top::config::xconfig_register_t::get_instance().get(std::string("static_waste_time"), set_waste_time);
    if (current_gmt_time - node_start_time < set_waste_time * 10000) {
        return;
    }
    if (!executed_archive) {
        elect_config_nodes(current_time);
    }
    return;
#endif

    XMETRICS_TIME_RECORD(XARCHIVE_ELECT "on_timer_all_time");
    XCONTRACT_ENSURE(SOURCE_ADDRESS() == SELF_ADDRESS().value(), u8"xrec_elect_archive_contract_t instance is triggled by others");
    XCONTRACT_ENSURE(SELF_ADDRESS().value() == sys_contract_rec_elect_archive_addr,
                     u8"xrec_elect_archive_contract_t instance is not triggled by sys_contract_rec_elect_archive_addr");
    XCONTRACT_ENSURE(current_time <= TIME(), u8"xrec_elect_archive_contract_t::on_timer current_time > consensus leader's time");
    XCONTRACT_ENSURE(current_time + XGET_ONCHAIN_GOVERNANCE_PARAMETER(archive_election_interval) / 2 > TIME(), u8"xrec_elect_archive_contract_t::on_timer retried too many times");
    xinfo("xrec_elect_archive_contract_t::archive_elect %" PRIu64, current_time);

    xrange_t<config::xgroup_size_t> range{1, XGET_ONCHAIN_GOVERNANCE_PARAMETER(max_archive_group_size)};

    auto standby_result_store =
        xvm::serialization::xmsgpack_t<xstandby_result_store_t>::deserialize_from_string_prop(*this, sys_contract_rec_standby_pool_addr, data::XPROPERTY_CONTRACT_STANDBYS_KEY);
    auto standby_network_result = standby_result_store.result_of(network_id()).network_result();

    auto property_names = data::election::get_property_name_by_addr(SELF_ADDRESS());
    for (auto const & property : property_names) {
        auto election_result_store = xvm::serialization::xmsgpack_t<xelection_result_store_t>::deserialize_from_string_prop(*this, property);
        auto & election_network_result = election_result_store.result_of(network_id());
        if (elect_group(common::xarchive_zone_id,
                        common::xdefault_cluster_id,
                        common::xdefault_group_id,
                        current_time,
                        current_time,
                        range,
                        standby_network_result,
                        election_network_result)) {
            xvm::serialization::xmsgpack_t<xelection_result_store_t>::serialize_to_string_prop(*this, property, election_result_store);
        }
    }
}

NS_END4
