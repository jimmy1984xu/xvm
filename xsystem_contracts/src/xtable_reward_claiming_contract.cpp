// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xvm/xsystem_contracts/xreward/xtable_reward_claiming_contract.h"

#include "xdata/xdatautil.h"
#include "xdata/xnative_contract_address.h"
#include "xmetrics/xmetrics.h"

NS_BEG4(top, xvm, system_contracts, reward)

xtop_table_reward_claiming_contract::xtop_table_reward_claiming_contract(common::xnetwork_id_t const & network_id) : xbase_t{network_id} {}

void xtop_table_reward_claiming_contract::setup() {
    for (auto i = 1; i <= xstake::XPROPERTY_SPLITED_NUM; i++) {
        std::string property{xstake::XPORPERTY_CONTRACT_VOTER_DIVIDEND_REWARD_KEY_BASE};
        property += "-" + std::to_string(i);
        MAP_CREATE(property);
    }
    MAP_CREATE(xstake::XPORPERTY_CONTRACT_NODE_REWARD_KEY);
}

xcontract::xcontract_base * xtop_table_reward_claiming_contract::clone() {
    return new xtop_table_reward_claiming_contract{network_id()};
}

void xtop_table_reward_claiming_contract::update_vote_reward_record(std::string const & account, xstake::xreward_record & record) {
    uint32_t sub_map_no = (utl::xxh32_t::digest(account) % xstake::XPROPERTY_SPLITED_NUM) + 1;
    std::string property{xstake::XPORPERTY_CONTRACT_VOTER_DIVIDEND_REWARD_KEY_BASE};
    property += "-" + std::to_string(sub_map_no);

    base::xstream_t stream(base::xcontext_t::instance());
    record.serialize_to(stream);
    auto value_str = std::string((char *)stream.data(), stream.size());
    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_set_property_contract_voter_dividend_reward_key");
        MAP_SET(property, account, value_str);
    }
}

void xtop_table_reward_claiming_contract::recv_voter_dividend_reward(uint64_t issuance_clock_height, std::map<std::string, top::xstake::uint128_t> const & rewards) {
    XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_recv_voter_dividend_reward");


    auto const & source_address = SOURCE_ADDRESS();
    auto const & self_address = SELF_ADDRESS();

    xdbg("[xtop_table_reward_claiming_contract::recv_voter_dividend_reward] pid: %d, self address: %s, source address: %s, issuance_clock_height: %llu",
        getpid(), self_address.c_str(), source_address.c_str(), issuance_clock_height);

    if (rewards.size() == 0) {
        xwarn("[xtop_table_reward_claiming_contract::recv_voter_dividend_reward] pid: %d, rewards size 0", getpid());
        return;
    }

    XCONTRACT_ENSURE(sys_contract_zec_reward_addr == source_address, "xtop_table_reward_claiming_contract::recv_voter_dividend_reward from invalid address: " + source_address);
    std::map<std::string, std::string> adv_votes;
    std::string base_addr{};
    uint32_t table_id{static_cast<uint32_t>(-1)};
    XCONTRACT_ENSURE(data::xdatautil::extract_parts(self_address.value(), base_addr, table_id),
                     "xtop_table_reward_claiming_contract::recv_voter_dividend_reward: extract table id failed");

    try {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_get_property_contract_pollable_key");
        MAP_COPY_GET(xstake::XPORPERTY_CONTRACT_POLLABLE_KEY, adv_votes, data::xdatautil::serialize_owner_str(sys_contract_sharding_vote_addr, table_id));
    } catch (std::runtime_error & e) {
        xdbg("[xtop_table_reward_claiming_contract::recv_voter_dividend_reward] MAP_COPY_GET XPORPERTY_CONTRACT_POLLABLE_KEY error:%s", e.what());
    }

    auto add_voter_node_reward = [&](xstake::xreward_record & record, std::string adv, top::xstake::uint128_t reward, uint64_t issuance_clock_height) {
        bool found = false;
        for (auto & node_reward : record.node_rewards) {
            if (node_reward.account == adv) {
                found = true;
                node_reward.accumulated += reward;
                node_reward.unclaimed   += reward;
                node_reward.issue_time  = issuance_clock_height;
                break;
            }
        }
        if (!found) {
            xstake::node_record_t voter_node_reward;
            voter_node_reward.account       = adv;
            voter_node_reward.accumulated   = reward;
            voter_node_reward.unclaimed     = reward;
            voter_node_reward.issue_time    = issuance_clock_height;
            record.node_rewards.push_back(voter_node_reward);
        }
        record.accumulated += reward;
        record.unclaimed += reward;
    };

    auto calc_voter_reward = [&](const std::map<std::string, std::string> & voters) {
        for (auto const & entity : voters) {
            auto const & account = entity.first;
            auto const & vote_table_str = entity.second;
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)vote_table_str.c_str(), (uint32_t)vote_table_str.size());
            std::map<std::string, uint64_t> votes_table;
            stream >> votes_table;

            top::xstake::uint128_t node_vote_reward = 0;
            top::xstake::uint128_t voter_node_reward = 0;
            uint64_t node_total_votes = 0;
            uint64_t voter_node_votes = 0;
            xstake::xreward_record record;
            get_vote_reward_record(account, record); // not care return value hear
            record.issue_time = issuance_clock_height;
            for (auto const & adv_vote : votes_table) {
                auto const & adv = adv_vote.first;
                auto iter = rewards.find(adv);
                if (iter != rewards.end()) {
                    node_vote_reward = iter->second;
                    //node_vote_reward = static_cast<xuint128_t>(iter->second.first * xstake::REWARD_PRECISION) + iter->second.second;
                } else {
                    node_vote_reward = 0;
                    continue;
                }
                auto iter2 = adv_votes.find(adv);
                if (iter2 != adv_votes.end()) {
                    node_total_votes = base::xstring_utl::touint64(iter2->second);
                } else {
                    node_total_votes = 0;
                    continue;
                }
                voter_node_votes = votes_table[adv];
                voter_node_reward = node_vote_reward * voter_node_votes / node_total_votes;
                //voter_node_reward = static_cast<xuint128_t>(xstake::REWARD_PRECISION) * voter_node_votes / node_total_votes * node_vote_reward;
                add_voter_node_reward(record, adv, voter_node_reward, issuance_clock_height);

                xdbg(
                    "[xtop_table_reward_claiming_contract::recv_voter_dividend_reward] voter: %s, adv node: %s, node_vote_reward: [%llu, %u], node_total_votes: %llu, voter_node_votes: "
                    "%llu, voter_node_reward: "
                    "[%llu, %u], pid: %d",
                    account.c_str(),
                    adv.c_str(),
                    static_cast<uint64_t>(node_vote_reward / REWARD_PRECISION),
                    static_cast<uint32_t>(node_vote_reward % REWARD_PRECISION),
                    node_total_votes,
                    voter_node_votes,
                    static_cast<uint64_t>(voter_node_reward / REWARD_PRECISION),
                    static_cast<uint32_t>(voter_node_reward % REWARD_PRECISION),
                    getpid());
            }

            update_vote_reward_record(account, record);
        }
    };

    for (auto i = 1; i <= xstake::XPROPERTY_SPLITED_NUM; ++i) {
        std::string property_name{xstake::XPORPERTY_CONTRACT_VOTES_KEY_BASE};
        property_name += "-" + std::to_string(i);

        std::map<std::string, std::string> voters;

        {
            XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_get_property_contract_votes_key");
            MAP_COPY_GET(property_name, voters, data::xdatautil::serialize_owner_str(sys_contract_sharding_vote_addr, table_id));
        }

        xdbg("[xtop_table_reward_claiming_contract::recv_voter_dividend_reward] vote maps %s size: %d, pid: %d", property_name.c_str(), voters.size(), getpid());
        calc_voter_reward(voters);
    }
}

void xtop_table_reward_claiming_contract::claimVoterDividend() {
    XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_claim_voter_dividend");

    const std::string & account = SOURCE_ADDRESS();
    xstake::xreward_record reward_record;
    XCONTRACT_ENSURE(get_vote_reward_record(account, reward_record) == 0, "claimVoterDividend account no reward");
    uint64_t cur_time = TIME();
    xdbg("[xtop_table_reward_claiming_contract::claimVoterDividend] balance:%llu, account: %s, pid: %d, cur_time: %llu, last_claim_time: %llu\n",
         GET_BALANCE(),
         account.c_str(),
         getpid(),
         cur_time,
         reward_record.last_claim_time);
    auto min_voter_dividend = XGET_ONCHAIN_GOVERNANCE_PARAMETER(min_voter_dividend);
    XCONTRACT_ENSURE(reward_record.unclaimed > min_voter_dividend, "claimVoterDividend no enough reward");

    // transfer to account
    xinfo("[xtop_table_reward_claiming_contract::claimVoterDividend] timer round: %" PRIu64 ", account: %s, reward:: [%llu, %u], pid:%d\n",
         TIME(),
         account.c_str(),
         static_cast<uint64_t>(reward_record.unclaimed / REWARD_PRECISION),
         static_cast<uint32_t>(reward_record.unclaimed % REWARD_PRECISION),
         getpid());
    XMETRICS_PACKET_INFO("sysContract_tableRewardClaiming_claim_voter_dividend", "timer round", std::to_string(TIME()), "source address", account, "reward", std::to_string(static_cast<uint64_t>(reward_record.unclaimed / REWARD_PRECISION)));
    TRANSFER(account, static_cast<uint64_t>(reward_record.unclaimed / REWARD_PRECISION));

    reward_record.unclaimed -= reward_record.unclaimed / REWARD_PRECISION * REWARD_PRECISION;
    reward_record.last_claim_time = cur_time;
    for (auto & node_reward : reward_record.node_rewards) {
        node_reward.unclaimed = 0;
        node_reward.last_claim_time = cur_time;
    }
    update_vote_reward_record(account, reward_record);
}

void xtop_table_reward_claiming_contract::update_working_reward_record(std::string const & account, xstake::xreward_node_record & record) {
    base::xstream_t stream(base::xcontext_t::instance());
    record.serialize_to(stream);
    auto value_str = std::string((char *)stream.data(), stream.size());
    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_set_property_contract_node_reward_key");
        MAP_SET(xstake::XPORPERTY_CONTRACT_NODE_REWARD_KEY, account, value_str);
    }
}

void xtop_table_reward_claiming_contract::update(std::string const & node_account, uint64_t issuance_clock_height, top::xstake::uint128_t reward) {
    auto const & self_address = SELF_ADDRESS();
    xdbg("[xtop_table_reward_claiming_contract::update] self_address: %s, account: %s, reward: [%llu, %u], pid: %d",
        self_address.c_str(), node_account.c_str(),
        static_cast<uint64_t>(reward / REWARD_PRECISION),
        static_cast<uint32_t>(reward % REWARD_PRECISION),
        getpid());

    // update node rewards table
    std::string value_str;
    xstake::xreward_node_record record;
    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_get_property_contract_node_reward_key");
        int32_t ret = MAP_GET2(xstake::XPORPERTY_CONTRACT_NODE_REWARD_KEY, node_account, value_str);
        // here if not success, means account has no reward record yet, so value_str is empty, using above record directly
        if (ret) xwarn("[xtop_table_reward_claiming_contract::update] get property empty, node account %s", node_account.c_str());
        if (!value_str.empty()) {
            base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.c_str(), (uint32_t)value_str.size());
            record.serialize_from(stream);
        }
    }


    record.m_accumulated    += reward;
    record.m_unclaimed      += reward;
    record.m_issue_time     = issuance_clock_height;

    base::xstream_t stream(base::xcontext_t::instance());
    record.serialize_to(stream);

    value_str = std::string((char *)stream.data(), stream.size());
    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_set_property_contract_node_reward_key");
        MAP_SET(xstake::XPORPERTY_CONTRACT_NODE_REWARD_KEY, node_account, value_str);
    }
}

void xtop_table_reward_claiming_contract::recv_node_reward(uint64_t issuance_clock_height, std::map<std::string, top::xstake::uint128_t> const & rewards) {
    XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_recv_node_reward");

    auto const & source_address = SOURCE_ADDRESS();
    auto const & self_address = SELF_ADDRESS();

    xdbg("[xtop_table_reward_claiming_contract::recv_node_reward] pid: %d, source_address: %s, self_address: %s, issuance_clock_height:%llu, rewards size: %d",
         getpid(),
         source_address.c_str(),
         self_address.c_str(),
         issuance_clock_height,
         rewards.size());

    XCONTRACT_ENSURE(sys_contract_zec_reward_addr == source_address,
                     "[xtop_table_reward_claiming_contract::recv_node_reward] working reward is not from zec workload contract but from " + source_address);

    for (auto const & account_reward : rewards) {
        auto const & account = account_reward.first;
        auto const & reward = account_reward.second;

        xdbg("[xtop_table_reward_claiming_contract::recv_node_reward] pid:%d, account: %s, reward: [%llu, %u]\n",
            getpid(), account.c_str(),
            static_cast<uint64_t>(reward / REWARD_PRECISION),
            static_cast<uint32_t>(reward % REWARD_PRECISION));

        // update node rewards
        update(account, issuance_clock_height, reward);
    }
}

void xtop_table_reward_claiming_contract::claimNodeReward() {
    XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_claim_node_reward");

    const std::string & account = SOURCE_ADDRESS();
    xstake::xreward_node_record reward_record;
    XCONTRACT_ENSURE(get_working_reward_record(account, reward_record) == 0, "claimNodeReward node account no reward");
    uint64_t cur_time = TIME();
    xdbg("[xtop_table_reward_claiming_contract::claimNodeReward] balance:%llu, account: %s, pid: %d, cur_time: %llu, last_claim_time: %llu\n",
         GET_BALANCE(),
         account.c_str(),
         getpid(),
         cur_time,
         reward_record.m_last_claim_time);
    auto min_node_reward = XGET_ONCHAIN_GOVERNANCE_PARAMETER(min_node_reward);
    XCONTRACT_ENSURE(static_cast<uint64_t>(reward_record.m_unclaimed / REWARD_PRECISION) > min_node_reward, "claimNodeReward: node no enough reward");

    // transfer to account
    xinfo("[xtop_table_reward_claiming_contract::claimNodeReward] timer round: %" PRIu64 ", account: %s, reward: [%llu, %u], pid:%d\n",
         TIME(),
         account.c_str(),
         static_cast<uint64_t>(reward_record.m_unclaimed / REWARD_PRECISION),
         static_cast<uint32_t>(reward_record.m_unclaimed % REWARD_PRECISION),
         getpid());

    XMETRICS_PACKET_INFO("sysContract_tableRewardClaiming_claim_node_reward", "timer round", std::to_string(TIME()), "source address", account, "reward", std::to_string(static_cast<uint64_t>(reward_record.m_unclaimed / REWARD_PRECISION)));
    TRANSFER(account, static_cast<uint64_t>(reward_record.m_unclaimed / REWARD_PRECISION));

    reward_record.m_unclaimed -= reward_record.m_unclaimed / REWARD_PRECISION * REWARD_PRECISION;
    reward_record.m_last_claim_time = cur_time;
    update_working_reward_record(account, reward_record);
}

int32_t xtop_table_reward_claiming_contract::get_working_reward_record(std::string const & account, xstake::xreward_node_record & record) {
    std::string value_str;

    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_get_property_contract_node_reward_key");
        int32_t ret = MAP_GET2(xstake::XPORPERTY_CONTRACT_NODE_REWARD_KEY, account, value_str);
        if (ret) {
            xdbg("[xtop_table_reward_claiming_contract::get_working_reward_record] account: %s not exist", account.c_str());
            return -1;
        }
    }

    if (!value_str.empty()) {
        base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.c_str(), (uint32_t)value_str.size());
        record.serialize_from(stream);
        return 0;
    }
    return -1;
}

int32_t xtop_table_reward_claiming_contract::get_vote_reward_record(std::string const & account, xstake::xreward_record & record) {
    uint32_t sub_map_no = (utl::xxh32_t::digest(account) % xstake::XPROPERTY_SPLITED_NUM) + 1;
    std::string property;
    property = property + xstake::XPORPERTY_CONTRACT_VOTER_DIVIDEND_REWARD_KEY_BASE + "-" + std::to_string(sub_map_no);

    std::string value_str;

    {
        XMETRICS_TIME_RECORD("sysContract_tableRewardClaiming_get_property_contract_voter_dividend_reward_key");
        if (!MAP_FIELD_EXIST(property, account)) {
            xdbg("[xtop_table_reward_claiming_contract::get_vote_reward_record] property: %s, account %s not exist", property.c_str(), account.c_str());
            return -1;
        } else {
            value_str = MAP_GET(property, account);
        }
    }

    if (!value_str.empty()) {
        base::xstream_t stream(base::xcontext_t::instance(), (uint8_t *)value_str.c_str(), (uint32_t)value_str.size());
        record.serialize_from(stream);
        return 0;
    }
    return -1;
}

NS_END4
