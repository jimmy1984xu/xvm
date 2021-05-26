// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xchain_upgrade/xchain_upgrade_center.h"
#include "xcommon/xip.h"
#include "xdata/xgenesis_data.h"
#include "xdata/xfull_tableblock.h"
#include "xstake/xstake_algorithm.h"
#include "xvm/manager/xcontract_manager.h"
#include "xvm/xsystem_contracts/xslash/xzec_slash_info_contract.h"


using namespace top::data;

NS_BEG3(top, xvm, xcontract)

#define SLASH_DELETE_PROPERTY  "SLASH_DELETE_PROPERTY"

xzec_slash_info_contract::xzec_slash_info_contract(common::xnetwork_id_t const & network_id) : xbase_t{network_id} {}

void xzec_slash_info_contract::setup() {
    // initialize map key
    MAP_CREATE(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY);
    MAP_CREATE(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY);

    MAP_CREATE(xstake::XPROPERTY_CONTRACT_EXTENDED_FUNCTION_KEY);
    MAP_SET(xstake::XPROPERTY_CONTRACT_EXTENDED_FUNCTION_KEY, SLASH_DELETE_PROPERTY, "false");
}

void xzec_slash_info_contract::summarize_slash_info(std::string const & slash_info) {
    XMETRICS_TIME_RECORD("sysContract_zecSlash_summarize_slash_info");
    auto const & account = SELF_ADDRESS();
    auto const & source_addr = SOURCE_ADDRESS();

    std::string base_addr = "";
    uint32_t table_id = 0;
    XCONTRACT_ENSURE(data::xdatautil::extract_parts(source_addr, base_addr, table_id), "source address extract base_addr or table_id error!");
    xdbg("[xzec_slash_info_contract][summarize_slash_info] self_account %s, source_addr %s, base_addr %s\n", account.c_str(), source_addr.c_str(), base_addr.c_str());
    XCONTRACT_ENSURE(base_addr == top::sys_contract_sharding_slash_info_addr, "invalid source addr's call!");

    xinfo("[xzec_slash_info_contract][summarize_slash_info] table contract report slash info, SOURCE_ADDRESS: %s, pid:%d, ", source_addr.c_str(), getpid());

    xunqualified_node_info_t summarize_info;
    std::string value_str;

    try {
        XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_unqualified_node_key");
        if (MAP_FIELD_EXIST(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE"))
            value_str = MAP_GET(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE");
    } catch (std::runtime_error const & e) {
        xwarn("[xzec_slash_info_contract][summarize_slash_info] read summarized slash info error:%s", e.what());
        throw;
    }

    if (!value_str.empty()) {
        base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
        summarize_info.serialize_from(stream);
    }

    value_str.clear();
    uint32_t summarize_tableblock_count = 0;
    try {
        XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_tableblock_num_key");
        if (MAP_FIELD_EXIST(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM")) {
            value_str = MAP_GET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM");
        }
    } catch (std::runtime_error & e) {
        xwarn("[xzec_slash_info_contract][summarize_slash_info] read summarized tableblock num error:%s", e.what());
        throw;
    }

    if (!value_str.empty()) {
        base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
        stream >> summarize_tableblock_count;
    }

    xunqualified_node_info_t node_info;
    std::uint32_t tableblock_count;
    base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)slash_info.data(), slash_info.size());
    node_info.serialize_from(stream);
    stream >> tableblock_count;

    for (auto const & item : node_info.auditor_info) {
        summarize_info.auditor_info[item.first].block_count += item.second.block_count;
        summarize_info.auditor_info[item.first].subset_count += item.second.subset_count;
    }

    for (auto const & item : node_info.validator_info) {
        summarize_info.validator_info[item.first].block_count += item.second.block_count;
        summarize_info.validator_info[item.first].subset_count += item.second.subset_count;
    }

    summarize_tableblock_count += tableblock_count;

    stream.reset();
    summarize_info.serialize_to(stream);
    {
        XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_unqualified_node_key");
        MAP_SET(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE", std::string((char *)stream.data(), stream.size()));
    }
    stream.reset();
    stream << summarize_tableblock_count;
    {
        XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_tableblock_num_key");
        MAP_SET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM", std::string((char *)stream.data(), stream.size()));
    }
    xkinfo("[xzec_slash_info_contract][summarize_slash_info]  table contract report slash info, auditor size: %zu, validator size: %zu, summarized tableblock num: %u, pid: %d",
         summarize_info.auditor_info.size(),
         summarize_info.validator_info.size(),
         summarize_tableblock_count,
         getpid());
}

void xzec_slash_info_contract::do_unqualified_node_slash(common::xlogic_time_t const timestamp) {
    XMETRICS_TIME_RECORD("sysContract_zecSlash_do_unqualified_node_slash");
    auto const & account = SELF_ADDRESS();
    auto const & source_addr = SOURCE_ADDRESS();

    std::string base_addr = "";
    uint32_t table_id = 0;
    XCONTRACT_ENSURE(data::xdatautil::extract_parts(source_addr, base_addr, table_id), "source address extract base_addr or table_id error!");
    xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] self_account %s, source_addr %s, base_addr %s\n", account.c_str(), source_addr.c_str(), base_addr.c_str());

    XCONTRACT_ENSURE(source_addr == account.value() && source_addr == top::sys_contract_zec_slash_info_addr, "invalid source addr's call!");


    xinfo("[xzec_slash_info_contract][do_unqualified_node_slash] do unqualified node slash info, time round: %" PRIu64 ": SOURCE_ADDRESS: %s, pid:%d",
         timestamp,
         SOURCE_ADDRESS().c_str(),
         getpid());

    /**
     *
     * get stored processed slash info
     *
     */
    xunqualified_node_info_t present_summarize_info;
    uint32_t present_tableblock_count = 0;
    std::string value_str;


    std::string delete_property = "false";
    try {
        XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_extend_key");
        delete_property = MAP_GET(xstake::XPROPERTY_CONTRACT_EXTENDED_FUNCTION_KEY, SLASH_DELETE_PROPERTY);
    } catch (std::runtime_error const & e) {
        xwarn("[xzec_slash_info_contract][[do_unqualified_node_slash] read extend key error:%s", e.what());
        throw;
    }
    xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] time round: %" PRIu64 ", extend key value: %s\n", timestamp, delete_property.c_str());

    if (delete_property == "true") {
        {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_remove_property_contract_unqualified_node_key");
            MAP_REMOVE(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE");
        }
        {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_remove_property_contract_tableblock_num_key");
            MAP_REMOVE(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM");
        }

        {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_extend_key");
            MAP_SET(xstake::XPROPERTY_CONTRACT_EXTENDED_FUNCTION_KEY, SLASH_DELETE_PROPERTY, "false");
        }

    } else {
        try {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_tableblock_num_key");
            if (MAP_FIELD_EXIST(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM")) {
                value_str = MAP_GET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM");
            }
        } catch (std::runtime_error const & e) {
            xwarn("[xzec_slash_info_contract][[do_unqualified_node_slash] read summarized tableblock num error:%s", e.what());
            throw;
        }
        // normally only first time will be empty(property not create yet), means tableblock count is zero, so no need else branch
        if (!value_str.empty()) {
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
            stream >> present_tableblock_count;
            xdbg("[xzec_slash_info_contract][do_unqualified_node_slash]  current summarized tableblock num is: %u", present_tableblock_count);
        }

        value_str.clear();
        try {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_unqualified_node_key");
            if (MAP_FIELD_EXIST(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE")) {
                value_str = MAP_GET(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE");
            }
        } catch (std::runtime_error const & e) {
            xwarn("[xzec_slash_info_contract][do_unqualified_node_slash] read summarized slash info error:%s", e.what());
            throw;
        }
        // normally only first time will be empty(property not create yet), means height is zero, so no need else branch
        if (!value_str.empty()) {
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
            present_summarize_info.serialize_from(stream);
        }

    }


    #ifdef DEBUG
        print_table_height_info();
        print_summarize_info(present_summarize_info);
        xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] present tableblock num is %u", present_tableblock_count);
    #endif


    /**
     * start process this time's fulltable blcok info, accumulate the block info & num
     *
     */
    xunqualified_node_info_t summarize_info = present_summarize_info;
    uint32_t summarize_tableblock_count = present_tableblock_count;
    std::vector<uint64_t> table_height(enum_vledger_const::enum_vbucket_has_tables_count, 0); // force ensure initial value is zero
    // process the fulltable of 256 table
    for (auto i = 0; i < enum_vledger_const::enum_vbucket_has_tables_count; ++i) {
        std::string height_key = std::to_string(i);
        uint64_t read_height = 0;
        std::string value_str;
        try {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_fulltableblock_height_key");
            if (MAP_FIELD_EXIST(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, height_key)) {
                value_str = MAP_GET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, height_key);
            }
        } catch (std::runtime_error & e) {
            xwarn("[xzec_slash_info_contract][get_next_fulltableblock] read full tableblock height error:%s", e.what());
            throw;
        }

        // normally only first time will be empty(property not create yet), means height is zero, so no need else branch
        if (!value_str.empty()) {
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
            stream >> read_height;
            xdbg("[xzec_slash_info_contract][do_unqualified_node_slash]  last read full tableblock height is: %" PRIu64, read_height);
        }

        std::string table_owner = xdatautil::serialize_owner_str(sys_contract_sharding_table_block_addr, i);
        auto time_interval = XGET_CONFIG(slash_fulltable_interval);
        auto const& full_blocks = get_next_fulltableblock(common::xaccount_address_t{table_owner}, (uint64_t)time_interval, read_height);
        if (!full_blocks.empty()) {
            summarize_tableblock_count += full_blocks[0]->get_height() - read_height;
            table_height[i] = full_blocks[0]->get_height();
            xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] update for latest full tableblock,  full tableblock height is: %" PRIu64 ", read_height: %" PRIu64, full_blocks[0]->get_height(), read_height);
            // process every fulltableblock statistic data
            for (std::size_t block_index = 0; block_index < full_blocks.size(); ++block_index) {

                xfull_tableblock_t* full_tableblock = dynamic_cast<xfull_tableblock_t*>(full_blocks[block_index].get());
                auto node_service = contract::xcontract_manager_t::instance().get_node_service();
                auto fulltable_statisitc_data = full_tableblock->get_table_statistics();
                auto const node_info = process_statistic_data(fulltable_statisitc_data, node_service);
                accumulate_node_info(node_info, summarize_info);
            }
        }

    }



    bool update_property = false;
    {
        {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_fulltableblock_height_key");
            for (std::size_t i = 0; i < table_height.size(); ++i) {
                // for optimize
                if (0 != table_height[i]) {
                    update_property = true;
                    base::xstream_t stream{base::xcontext_t::instance()};
                    stream << table_height[i];
                    MAP_SET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, std::to_string(i), std::string((char *)stream.data(), stream.size()));
                }
            }

        }

        if (update_property) {

            // for rpc and following previous flow
            {
                XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_unqualified_node_key");
                base::xstream_t stream{base::xcontext_t::instance()};
                summarize_info.serialize_to(stream);
                MAP_SET(xstake::XPORPERTY_CONTRACT_UNQUALIFIED_NODE_KEY, "UNQUALIFIED_NODE", std::string((char *)stream.data(), stream.size()));
            }
            {
                XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_tableblock_num_key");
                base::xstream_t stream{base::xcontext_t::instance()};
                stream << summarize_tableblock_count;
                MAP_SET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, "TABLEBLOCK_NUM", std::string((char *)stream.data(), stream.size()));
            }

        }

    }



    #ifdef DEBUG
        print_table_height_info();
        print_summarize_info(summarize_info);
        xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] summarize_tableblock_count current, time round: %" PRIu64
            ": SOURCE_ADDRESS: %s, pid:%d, tableblock_count:%u",
            timestamp,
            SOURCE_ADDRESS().c_str(),
            getpid(),
            summarize_tableblock_count);
    #endif

    if (summarize_tableblock_count < XGET_ONCHAIN_GOVERNANCE_PARAMETER(punish_interval_table_block)) {
        xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] summarize_tableblock_count not enought, time round: %" PRIu64
            ": SOURCE_ADDRESS: %s, pid:%d, tableblock_count:%u",
            timestamp,
            SOURCE_ADDRESS().c_str(),
            getpid(),
            summarize_tableblock_count);
        return;
    }



    auto node_to_action = filter_nodes(summarize_info);
    xdbg("[xzec_slash_info_contract][filter_slashed_nodes] remove summarize info, time round: %" PRIu64, timestamp);

    if (!node_to_action.empty()) {
        base::xstream_t stream{base::xcontext_t::instance()};
        VECTOR_OBJECT_SERIALIZE2(stream, node_to_action);
        std::string punish_node_str = std::string((char *)stream.data(), stream.size());
        {
            stream.reset();
            stream << punish_node_str;
            xinfo("[xzec_slash_info_contract][do_unqualified_node_slash] call register contract slash_unqualified_node, time round: %" PRIu64, timestamp);
            XMETRICS_PACKET_INFO("sysContract_zecSlash", "effective slash timer round", std::to_string(timestamp));
            CALL(common::xaccount_address_t{sys_contract_rec_registration_addr}, "slash_unqualified_node", std::string((char *)stream.data(), stream.size()));
        }
    } else {
        xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] filter slash node empty! time round %" PRIu64, timestamp);
    }

}

std::vector<xaction_node_info_t> xzec_slash_info_contract::filter_nodes(xunqualified_node_info_t const & summarize_info) {
    std::vector<xaction_node_info_t> nodes_to_slash{};

    if (summarize_info.auditor_info.empty() && summarize_info.validator_info.empty()) {
        xwarn("[xzec_slash_info_contract][filter_slashed_nodes] the summarized node info is empty!");
        return nodes_to_slash;
    } else {
        // do filter
        auto slash_vote = XGET_ONCHAIN_GOVERNANCE_PARAMETER(sign_block_publishment_threshold_value);
        auto slash_persent = XGET_ONCHAIN_GOVERNANCE_PARAMETER(sign_block_ranking_publishment_threshold_value);
        auto award_vote = XGET_ONCHAIN_GOVERNANCE_PARAMETER(sign_block_ranking_reward_threshold_value);
        auto award_persent = XGET_ONCHAIN_GOVERNANCE_PARAMETER(sign_block_reward_threshold_value);

        // summarized info
        nodes_to_slash = filter_helper(summarize_info, slash_vote, slash_persent, award_vote, award_persent);

        {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_set_property_contract_extend_key");
            MAP_SET(xstake::XPROPERTY_CONTRACT_EXTENDED_FUNCTION_KEY, SLASH_DELETE_PROPERTY, "true");
        }

    }

    xkinfo("[xzec_slash_info_contract][filter_slashed_nodes] the filter node to slash: size: %zu", nodes_to_slash.size());
    return nodes_to_slash;
}

std::vector<xaction_node_info_t> xzec_slash_info_contract::filter_helper(xunqualified_node_info_t const & node_map, uint32_t slash_vote, uint32_t slash_persent, uint32_t award_vote, uint32_t award_persent) {
    std::vector<xaction_node_info_t> res{};

    // do filter
    std::vector<xunqualified_filter_info_t> node_to_action{};
    for (auto const & node : node_map.auditor_info) {
        xunqualified_filter_info_t info;
        info.node_id = node.first;
        info.node_type = common::xnode_type_t::consensus_auditor;
        info.vote_percent = node.second.block_count * 100 / node.second.subset_count;
        node_to_action.emplace_back(info);
    }

    std::sort(node_to_action.begin(), node_to_action.end(), [](xunqualified_filter_info_t const & lhs, xunqualified_filter_info_t const & rhs) {
        return lhs.vote_percent < rhs.vote_percent;
    });
    // uint32_t slash_size = node_to_slash.size() * slash_persent / 100 ?  node_to_slash.size() * slash_persent / 100 : 1;
    uint32_t slash_size = node_to_action.size() * slash_persent / 100;
    for (size_t i = 0; i < slash_size; ++i) {
        if (node_to_action[i].vote_percent < slash_vote || 0 == node_to_action[i].vote_percent) {
            res.push_back(xaction_node_info_t{node_to_action[i].node_id, node_to_action[i].node_type});
        }
    }

    uint32_t award_size = node_to_action.size() * award_persent / 100;
    for (int i = (int)node_to_action.size() - 1; i >= (int)(node_to_action.size() - award_size); --i) {
        if (node_to_action[i].vote_percent > award_vote) {
            res.push_back(xaction_node_info_t{node_to_action[i].node_id, node_to_action[i].node_type, false});
        }
    }

    node_to_action.clear();
    for (auto const & node : node_map.validator_info) {
        xunqualified_filter_info_t info;
        info.node_id = node.first;
        info.node_type = common::xnode_type_t::consensus_validator;
        info.vote_percent = node.second.block_count * 100 / node.second.subset_count;
        node_to_action.emplace_back(info);
    }

    std::sort(node_to_action.begin(), node_to_action.end(), [](xunqualified_filter_info_t const & lhs, xunqualified_filter_info_t const & rhs) {
        return lhs.vote_percent < rhs.vote_percent;
    });

    // uint32_t slash_size = node_to_slash.size() * slash_persent / 100 ?  node_to_slash.size() * slash_persent / 100 : 1;
    slash_size = node_to_action.size() * slash_persent / 100;
    for (size_t i = 0; i < slash_size; ++i) {
        if (node_to_action[i].vote_percent < slash_vote || 0 == node_to_action[i].vote_percent) {
            res.push_back(xaction_node_info_t{node_to_action[i].node_id, node_to_action[i].node_type});
        }
    }

    award_size = node_to_action.size() * award_persent / 100;
    for (int i = (int)node_to_action.size() - 1; i >= (int)(node_to_action.size() - award_size); --i) {
        if (node_to_action[i].vote_percent > award_vote) {
            res.push_back(xaction_node_info_t{node_to_action[i].node_id, node_to_action[i].node_type, false});
        }
    }

    return res;
}

void xzec_slash_info_contract::print_summarize_info(data::xunqualified_node_info_t const & summarize_slash_info) {
    std::string out = "";
    for (auto const & item : summarize_slash_info.auditor_info) {
        out += item.first.value();
        out += "|" + std::to_string(item.second.block_count);
        out += "|" + std::to_string(item.second.subset_count) + "|";
    }

    for (auto const & item : summarize_slash_info.validator_info) {
        out += item.first.value();
        out += "|" + std::to_string(item.second.block_count);
        out += "|" + std::to_string(item.second.subset_count) + "|";
    }

    xdbg("[xzec_slash_info_contract][print_summarize_info] summarize info: %s", out.c_str());
}

void xzec_slash_info_contract::print_table_height_info() {
    std::string out = "|";

    for (auto i = 0; i < enum_vledger_const::enum_vbucket_has_tables_count; ++i) {
        std::string height_key = std::to_string(i);
        std::string value_str;
        try {
            XMETRICS_TIME_RECORD("sysContract_zecSlash_get_property_contract_fulltableblock_height_key");
            if (MAP_FIELD_EXIST(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, height_key)) {
                value_str = MAP_GET(xstake::XPROPERTY_CONTRACT_TABLEBLOCK_NUM_KEY, height_key);
            }
        } catch (std::runtime_error & e) {
            xwarn("[xzec_slash_info_contract][get_next_fulltableblock] read full tableblock height error:%s", e.what());
            throw;
        }

        if (!value_str.empty()) {
            uint64_t height;
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.data(), value_str.size());
            stream >> height;
            if (height != 0) {
                out += std::to_string(i) + "-" + std::to_string(height) + "|";
            }
        }

    }

    xdbg("[xzec_slash_info_contract][print_table_height_info] table height info: %s", out.c_str());
}

std::vector<base::xauto_ptr<data::xblock_t>> xzec_slash_info_contract::get_next_fulltableblock(common::xaccount_address_t const& owner, uint64_t time_interval, uint64_t last_read_height) const {
    std::vector<base::xauto_ptr<data::xblock_t>> res;
    auto blockchain_height = get_blockchain_height(owner.value());
    xdbg("[xzec_slash_info_contract][get_next_fulltableblock] tableblock owner: %s, current blockchain height: %" PRIu64 " current read height: %" PRIu64,
                                                            owner.value().c_str(), blockchain_height, last_read_height);
    base::xauto_ptr<data::xblock_t> tableblock = get_block_by_height(owner.value(), blockchain_height);
    auto last_fullblock_height  = tableblock->get_last_full_block_height();

    while (last_read_height < last_fullblock_height && last_fullblock_height != 0) {
        base::xauto_ptr<data::xblock_t> last_full_tableblock = get_block_by_height(owner.value(), last_fullblock_height);
        XCONTRACT_ENSURE(last_full_tableblock, "[xzec_slash_info_contract][get_next_fulltableblock] last full tableblock is empty");
        XCONTRACT_ENSURE(last_full_tableblock->is_fulltable(), "[xzec_slash_info_contract][get_next_fulltableblock] last full tableblock is not full tableblock");
        XCONTRACT_ENSURE(TIME() > last_full_tableblock->get_clock(), "[xzec_slash_info_contract][get_next_fulltableblock] time order error");
        last_fullblock_height = last_full_tableblock->get_last_full_block_height();
        if (TIME() - last_full_tableblock->get_clock() < time_interval) { // less than time interval, do not process
            xdbg("[xzec_slash_info_contract][get_next_fulltableblock] clock interval not statisfy, now: %" PRIu64 ", fulltableblock owner: %s, height: %" PRIu64 ", clock: %" PRIu64,
                                                                 TIME(), owner.value().c_str(), last_full_tableblock->get_height(), last_full_tableblock->get_clock());
        } else {
            xdbg("[xzec_slash_info_contract][get_next_fulltableblock] fulltableblock owner: %s, height: %" PRIu64, owner.value().c_str(), last_full_tableblock->get_height());
            res.push_back(std::move(last_full_tableblock));
        }
    }

    return res;
}

xunqualified_node_info_t xzec_slash_info_contract::process_statistic_data(top::data::xstatistics_data_t const& block_statistic_data,  base::xvnodesrv_t * node_service) {
    xunqualified_node_info_t res_node_info;

    // process one full tableblock statistic data
    for (auto const static_item: block_statistic_data.detail) {
        auto elect_statistic = static_item.second;
        for (auto const group_item: elect_statistic.group_statistics_data) {
            xgroup_related_statistics_data_t const& group_account_data = group_item.second;
            common::xgroup_address_t const& group_addr = group_item.first;
            xvip2_t const& group_xvip2 = top::common::xip2_t{
                group_addr.network_id(),
                group_addr.zone_id(),
                group_addr.cluster_id(),
                group_addr.group_id(),
                common::xdefault_network_version,
                (uint16_t)group_account_data.account_statistics_data.size(),
                static_item.first
            };
            // process auditor group
            if (top::common::has<top::common::xnode_type_t::auditor>(group_addr.type())) {
                for (std::size_t slotid = 0; slotid < group_account_data.account_statistics_data.size(); ++slotid) {
                    auto account_addr = node_service->get_group(group_xvip2)->get_node(slotid)->get_account();
                    res_node_info.auditor_info[common::xnode_id_t{account_addr}].subset_count += group_account_data.account_statistics_data[slotid].vote_data.block_count;
                    res_node_info.auditor_info[common::xnode_id_t{account_addr}].block_count += group_account_data.account_statistics_data[slotid].vote_data.vote_count;
                    xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] incremental auditor data: {gourp id: %d, account addr: %s, slot id: %u, subset count: %u, block_count: %u}", group_addr.group_id().value(), account_addr.c_str(),
                        slotid, group_account_data.account_statistics_data[slotid].vote_data.block_count, group_account_data.account_statistics_data[slotid].vote_data.vote_count);
                }
            } else if (top::common::has<top::common::xnode_type_t::validator>(group_addr.type())) {// process validator group
                for (std::size_t slotid = 0; slotid < group_account_data.account_statistics_data.size(); ++slotid) {
                    auto account_addr = node_service->get_group(group_xvip2)->get_node(slotid)->get_account();
                    res_node_info.validator_info[common::xnode_id_t{account_addr}].subset_count += group_account_data.account_statistics_data[slotid].vote_data.block_count;
                    res_node_info.validator_info[common::xnode_id_t{account_addr}].block_count += group_account_data.account_statistics_data[slotid].vote_data.vote_count;
                    xdbg("[xzec_slash_info_contract][do_unqualified_node_slash] incremental validator data: {gourp id: %d, account addr: %s, slot id: %u, subset count: %u, block_count: %u}", group_addr.group_id().value(), account_addr.c_str(),
                        slotid, group_account_data.account_statistics_data[slotid].vote_data.block_count, group_account_data.account_statistics_data[slotid].vote_data.vote_count);
                }

            } else { // invalid group
                xwarn("[xzec_slash_info_contract][do_unqualified_node_slash] invalid group id: %d", group_addr.group_id().value());
                std::error_code ec{ xvm::enum_xvm_error_code::enum_vm_exception };
                top::error::throw_error(ec, "[xzec_slash_info_contract][do_unqualified_node_slash] invalid group");
            }

        }

    }

    return res_node_info;
 }

void  xzec_slash_info_contract::accumulate_node_info(xunqualified_node_info_t const&  node_info, xunqualified_node_info_t& summarize_info) {

    for (auto const & item : node_info.auditor_info) {
        summarize_info.auditor_info[item.first].block_count += item.second.block_count;
        summarize_info.auditor_info[item.first].subset_count += item.second.subset_count;
    }

    for (auto const & item : node_info.validator_info) {
        summarize_info.validator_info[item.first].block_count += item.second.block_count;
        summarize_info.validator_info[item.first].subset_count += item.second.subset_count;
    }
}

NS_END3
