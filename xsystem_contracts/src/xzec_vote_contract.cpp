// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xvm/xsystem_contracts/xreward/xzec_vote_contract.h"

#include "xstore/xstore_error.h"
#include "xbasic/xutility.h"
#include "xdata/xgenesis_data.h"
#include "xbase/xutl.h"
#include "xstake/xstake_algorithm.h"
#include <iomanip>

using top::base::xstream_t;
using top::base::xcontext_t;
using top::base::xstring_utl;
using namespace top::data;

#if !defined (XZEC_MODULE)
#    define XZEC_MODULE "sysContract_"
#endif

#define XCONTRACT_PREFIX "vote_"

#define XVOTE_CONTRACT XZEC_MODULE XCONTRACT_PREFIX

NS_BEG2(top, xstake)

xzec_vote_contract::xzec_vote_contract(common::xnetwork_id_t const & network_id)
    : xbase_t{ network_id } {
}

void xzec_vote_contract::setup() {
    // save shard total tickets
    MAP_CREATE(XPORPERTY_CONTRACT_TICKETS_KEY);
    /*{
        // test
        std::map<std::string, std::string> contract_auditor_votes;
        auto table_address = "T20000MVfDLsBKVcy1wMp4CoEHWxUeBEAVBL9ZEa@0";

        uint64_t votes = 1000000;
        for (auto i = 0; i < 700; i++) {
            std::stringstream ss;
            ss << std::setw(40) << std::setfill('0') << i;
            auto node_account = ss.str();

            contract_auditor_votes[node_account] = base::xstring_utl::tostring(votes);
        }

        xstream_t stream(xcontext_t::instance());
        stream << contract_auditor_votes;
        std::string contract_adv_votes_str = std::string((const char*)stream.data(), stream.size());
        MAP_SET(XPORPERTY_CONTRACT_TICKETS_KEY, table_address, contract_adv_votes_str);
    }*/
}

int xzec_vote_contract::is_mainnet_activated() {
    xactivation_record record;

    std::string value_str = STRING_GET2(xstake::XPORPERTY_CONTRACT_GENESIS_STAGE_KEY, sys_contract_rec_registration_addr);
    if (!value_str.empty()) {
        base::xstream_t stream(base::xcontext_t::instance(),
                    (uint8_t*)value_str.c_str(), (uint32_t)value_str.size());
        record.serialize_from(stream);
    }
    xdbg("[xzec_vote_contract::is_mainnet_activated] activated: %d, pid:%d\n", record.activated, getpid());
    return record.activated;
};

void xzec_vote_contract::on_receive_shard_votes(std::map<std::string, std::string> contract_adv_votes) {
    XMETRICS_COUNTER_INCREMENT(XVOTE_CONTRACT "on_receive_shard_votes_Called", 1);
    XMETRICS_TIME_RECORD(XVOTE_CONTRACT "on_receive_shard_votes_ExecutionTime");
    auto const& source_address = SOURCE_ADDRESS();
    xdbg("[xzec_vote_contract::on_receive_shard_votes] contract addr: %s, contract_adv_votes size: %d, pid:%d\n",
        source_address.c_str(), contract_adv_votes.size(), getpid());

    std::string base_addr;
    uint32_t    table_id;
    if (!data::xdatautil::extract_parts(source_address, base_addr, table_id) || sys_contract_sharding_vote_addr != base_addr) {
        xwarn("[xzec_vote_contract::on_receive_shard_votes] invalid call from %s", source_address.c_str());
        return;
    }

    if ( !is_mainnet_activated() ) return;

#if defined DEBUG
     for (auto vote : contract_adv_votes) {
        xdbg("[xzec_vote_contract::on_receive_shard_votes] node account: %s, votes: %llu",
            vote.first.c_str(), base::xstring_utl::touint64(vote.second));
    }
#endif

    xstream_t stream(xcontext_t::instance());
    stream << contract_adv_votes;
    std::string contract_adv_votes_str = std::string((const char*)stream.data(), stream.size());
    {
        XMETRICS_TIME_RECORD(XVOTE_CONTRACT "XPORPERTY_CONTRACT_TICKETS_KEY_SetExecutionTime");
        MAP_SET(XPORPERTY_CONTRACT_TICKETS_KEY, source_address, contract_adv_votes_str);
    }

    XMETRICS_COUNTER_INCREMENT(XVOTE_CONTRACT "on_receive_shard_votes_Executed", 1);
}

bool xzec_vote_contract::handle_receive_shard_votes(uint64_t report_time, uint64_t last_report_time, std::map<std::string, std::string> const & contract_adv_votes, std::map<std::string, std::string> & merge_contract_adv_votes) {
    xdbg("[xzec_vote_contract::handle_receive_shard_votes] report vote table size: %d, original vote table size: %d",
            contract_adv_votes.size(), merge_contract_adv_votes.size());
    if (report_time < last_report_time) {
        return false;
    }
    if (report_time == last_report_time) {
        merge_contract_adv_votes.insert(contract_adv_votes.begin(), contract_adv_votes.end());
        xdbg("[xzec_vote_contract::handle_receive_shard_votes] same batch of vote report, report vote table size: %d, total size: %d",
            contract_adv_votes.size(), merge_contract_adv_votes.size());
    } else {
        merge_contract_adv_votes = contract_adv_votes;
    }
    return true;
}

void xzec_vote_contract::on_receive_shard_votes_v2(uint64_t report_time, std::map<std::string, std::string> const & contract_adv_votes) {
    XMETRICS_COUNTER_INCREMENT(XVOTE_CONTRACT "on_receive_shard_votes_Called", 1);
    XMETRICS_TIME_RECORD(XVOTE_CONTRACT "on_receive_shard_votes_ExecutionTime");
    auto const& source_address = SOURCE_ADDRESS();
    xdbg("[xzec_vote_contract::on_receive_shard_votes_v2] contract addr: %s, contract_adv_votes size: %d, report_time: %llu, pid:%d\n",
        source_address.c_str(), contract_adv_votes.size(), report_time, getpid());

    std::string base_addr;
    uint32_t    table_id;
    if (!data::xdatautil::extract_parts(source_address, base_addr, table_id) || sys_contract_sharding_vote_addr != base_addr) {
        xwarn("[xzec_vote_contract::on_receive_shard_votes_v2] invalid call from %s", source_address.c_str());
        XCONTRACT_ENSURE(false, "[xzec_vote_contract::on_receive_shard_votes_v2] invalid call");
    }

    if ( !is_mainnet_activated() ) return;

    if (!MAP_PROPERTY_EXIST(XPORPERTY_CONTRACT_VOTE_REPORT_TIME_KEY)) {
        MAP_CREATE(XPORPERTY_CONTRACT_VOTE_REPORT_TIME_KEY);
        MAP_SET(XPORPERTY_CONTRACT_VOTE_REPORT_TIME_KEY, source_address, base::xstring_utl::tostring(0));
    }

    std::string value_str;
    MAP_GET2(XPORPERTY_CONTRACT_VOTE_REPORT_TIME_KEY, source_address, value_str);
    uint64_t last_report_time = 0;
    if (!value_str.empty()) {
        last_report_time = base::xstring_utl::touint64(value_str);
        xdbg("[xzec_vote_contract::on_receive_shard_votes_v2] last_report_time: %llu", last_report_time);
    }
    MAP_SET(XPORPERTY_CONTRACT_VOTE_REPORT_TIME_KEY, source_address, base::xstring_utl::tostring(report_time));

    {
        XMETRICS_TIME_RECORD(XVOTE_CONTRACT "XPORPERTY_CONTRACT_TICKETS_KEY_SetExecutionTime");
        std::map<std::string, std::string> auditor_votes;
        std::string auditor_votes_str;
        MAP_GET2(XPORPERTY_CONTRACT_TICKETS_KEY, source_address, auditor_votes_str);
        if (!auditor_votes_str.empty()) {
            base::xstream_t votes_stream(base::xcontext_t::instance(), (uint8_t *)auditor_votes_str.c_str(), (uint32_t)auditor_votes_str.size());
            votes_stream >> auditor_votes;
        }
        if ( !handle_receive_shard_votes(report_time, last_report_time, contract_adv_votes, auditor_votes) ) {
            XCONTRACT_ENSURE(false, "[xzec_vote_contract::on_receive_shard_votes_v2] handle_receive_shard_votes fail");
        }

        xstream_t stream(xcontext_t::instance());
        stream << auditor_votes;

        std::string contract_adv_votes_str = std::string((const char*)stream.data(), stream.size());
        MAP_SET(XPORPERTY_CONTRACT_TICKETS_KEY, source_address, contract_adv_votes_str);
    }

    XMETRICS_COUNTER_INCREMENT(XVOTE_CONTRACT "on_receive_shard_votes_Executed", 1);
}

NS_END2

#undef XVOTE_CONTRACT
#undef XCONTRACT_PREFIX
#undef XZEC_MODULE
