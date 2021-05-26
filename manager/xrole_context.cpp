// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "xchain_timer/xchain_timer_face.h"
#include "xchain_upgrade/xchain_upgrade_center.h"
#include "xmbus/xevent_timer.h"
#include "xmbus/xevent_store.h"
#include "xvm/manager/xcontract_address_map.h"
#include "xvm/manager/xmessage_ids.h"
#include "xvm/manager/xrole_context.h"
#include "xvm/xvm_service.h"


#include <cinttypes>
#include <cmath>

NS_BEG2(top, contract)
using base::xstring_utl;
xrole_context_t::xrole_context_t(const observer_ptr<xstore_face_t> & store,
                                 const observer_ptr<store::xsyncvstore_t> & syncstore,
                                 const std::shared_ptr<xtxpool_service_v2::xrequest_tx_receiver_face> & unit_service,
                                 const std::shared_ptr<xvnetwork_driver_face_t> & driver,
                                 xcontract_info_t * info)
  : m_store(store), m_syncstore(syncstore), m_unit_service(unit_service), m_driver(driver), m_contract_info(info) {
    XMETRICS_COUNTER_INCREMENT("xvm_contract_role_context_counter", 1);
  }

xrole_context_t::~xrole_context_t() {
    XMETRICS_COUNTER_INCREMENT("xvm_contract_role_context_counter", -1);
    if (m_contract_info != nullptr) {
        delete m_contract_info;
    }
}

void xrole_context_t::on_block(const xevent_ptr_t & e, bool & event_broadcasted) {
    xdbg("xrole_context_t::on_block");
    if (!m_contract_info->has_monitors()) {
        return;
    }

    xblock_ptr_t block{};
    bool new_block{false};
    if(e->major_type == xevent_major_type_chain_timer) {
        auto event = (const mbus::xevent_chain_timer_ptr_t&) e;
        event->time_block->add_ref();
        block.attach((xblock_t*) event->time_block);
    } else if(e->major_type == xevent_major_type_store && e->minor_type == xevent_store_t::type_block_to_db) {
        block = mbus::extract_block_from(dynamic_xobject_ptr_cast<xevent_store_block_to_db_t>(e));
        // block = ((xevent_store_block_to_db_t *)e.get())->block;
        new_block = ((xevent_store_block_to_db_t *)e.get())->new_block;
    }

    if(block == nullptr) {
        return;
    }

    xdbg("[xrole_context_t::on_block] %s, %" PRIu64, block->get_account().c_str(), block->get_height());
    // if (block->get_height() < 1)
    //    return;

    auto address = common::xaccount_address_t{block->get_block_owner()};

    // broadcast
    if (new_block && m_contract_info->has_broadcasts()) {
        if (common::xaccount_address_t{data::account_address_to_block_address(m_contract_info->address)} == address) {
            // check leader
            common::xip_t validator_xip{block->get_cert()->get_validator().low_addr};  // TODO (payton)
            if (validator_xip.slot_id() == m_driver->address().slot_id() && !event_broadcasted) {
                event_broadcasted = true;
                switch (m_contract_info->broadcast_policy) {
                case enum_broadcast_policy_t::normal:
                    xdbg("xrole_context_t::on_block::broadcast, normal, block=%s", block->dump().c_str());
                    // broadcast(((xevent_store_block_to_db_t *)e.get())->block, m_contract_info->broadcast_types);
                    broadcast(mbus::extract_block_from(dynamic_xobject_ptr_cast<xevent_store_block_to_db_t>(e)), m_contract_info->broadcast_types);
                    break;
                case enum_broadcast_policy_t::fullunit:
                    xdbg("xrole_context_t::on_block::broadcast, fullunit, block addr %s", address.to_string().c_str());
                    if (block->is_fullunit()) {
                        assert(false);
                        // broadcast(((xevent_store_block_to_db_t *)e.get())->block, m_contract_info->broadcast_types);
                        broadcast(mbus::extract_block_from(dynamic_xobject_ptr_cast<xevent_store_block_to_db_t>(e)), m_contract_info->broadcast_types);
                    }
                    break;
                default:
                    // no broadcast
                    break;
                }
            }
        }
    }

    if (m_contract_info->has_block_monitors()) {
        xblock_monitor_info_t * info = m_contract_info->find(address);
        if (info == nullptr) {
            for (auto & pair : m_contract_info->monitor_map) {
                if (xcontract_address_map_t::match(address, pair.first)) {
                    info = pair.second;
                    break;
                }
            }
        }

        if (info != nullptr) {
            bool do_call{false};
            uint64_t block_timestamp{0};
            uint64_t onchain_timer_round{0};
            if (info->type == enum_monitor_type_t::timer) {
                xinfo("==== timer monitor");
                xtimer_block_monitor_info_t * timer_info = dynamic_cast<xtimer_block_monitor_info_t *>(info);
                assert(timer_info);
                auto time_interval = timer_info->get_interval();
                if (address == common::xaccount_address_t{sys_contract_beacon_timer_addr}) {
                    xdbg("[xrole_context_t::on_block] get timer block at %" PRIu64, block->get_height());
                    onchain_timer_round = block->get_height();
                    block_timestamp = block->get_timestamp();

                    if (is_scheduled_table_contract(m_contract_info->address) && valid_call(onchain_timer_round)) {

                        int table_num = m_driver->table_ids().size();
                        if (table_num == 0) {
                            xwarn("xrole_context_t::on_block: table_ids empty\n");
                            return;
                        }

                        int clock_interval = 1;
                        if (m_contract_info->address == common::xaccount_address_t{sys_contract_sharding_workload_addr}) {
                            clock_interval = XGET_ONCHAIN_GOVERNANCE_PARAMETER(tableworkload_report_schedule_interval);
                        } else if (m_contract_info->address == common::xaccount_address_t{sys_contract_sharding_slash_info_addr}) {
                            clock_interval = XGET_ONCHAIN_GOVERNANCE_PARAMETER(tableslash_report_schedule_interval);
                        }

                        if (m_table_contract_schedule.find(m_contract_info->address) != m_table_contract_schedule.end()) {
                            auto& schedule_info = m_table_contract_schedule[m_contract_info->address];
                            schedule_info.target_interval = clock_interval;
                            schedule_info.cur_interval++;
                            if (schedule_info.cur_interval == schedule_info.target_interval) {
                                schedule_info.cur_table = m_driver->table_ids().at(0) +  static_cast<uint16_t>((onchain_timer_round / clock_interval) % table_num);
                                xinfo("xrole_context_t::on_block: table contract schedule, contract address %s, timer %" PRIu64 ", schedule info:[%hu, %hu, %hu %hu]",
                                    m_contract_info->address.value().c_str(), onchain_timer_round, schedule_info.cur_interval, schedule_info.target_interval, schedule_info.clock_or_table, schedule_info.cur_table);
                                call_contract(onchain_timer_round, info, block_timestamp, schedule_info.cur_table);
                                schedule_info.cur_interval = 0;
                            }
                        } else { // have not schedule yet
                            xtable_schedule_info_t schedule_info(clock_interval, m_driver->table_ids().at(0) + static_cast<uint16_t>((onchain_timer_round / clock_interval) % table_num));
                            xinfo("xrole_context_t::on_block: table contract schedule initial, contract address %s, timer %" PRIu64 ", schedule info:[%hu, %hu, %hu %hu]",
                                    m_contract_info->address.value().c_str(), onchain_timer_round, schedule_info.cur_interval, schedule_info.target_interval, schedule_info.clock_or_table, schedule_info.cur_table);
                            call_contract(onchain_timer_round, info, block_timestamp, schedule_info.cur_table);
                            m_table_contract_schedule[m_contract_info->address] = schedule_info;
                        }

                        return;
                    }

                    bool first_blk = runtime_stand_alone(onchain_timer_round, m_contract_info->address);
                    if (time_interval != 0 && onchain_timer_round != 0 && ((first_blk && (onchain_timer_round % 3) == 0) || (!first_blk && (onchain_timer_round % time_interval) == 0))) {
                        do_call = true;
                    }
                    // if (time_interval == 0 ||
                    //     round == 0         ||
                    //     ((round % time_interval) != 0 && !runtime_stand_alone(onchain_timer_round, m_contract_info->address))) {
                    //     do_call = false;
                    // }
                    xinfo("==== get timer2 transaction %s, %llu, %u, %d", m_contract_info->address.value().c_str(), onchain_timer_round, time_interval, do_call);
                } else {
                    assert(0);
                }
            }
            if (do_call && valid_call(onchain_timer_round)) {
                xinfo("call_contract : %s , %llu ,%d", m_contract_info->address.value().c_str(), onchain_timer_round, static_cast<int32_t>(info->type));
                call_contract(onchain_timer_round, info, block_timestamp);
            }
        }
    }
}

bool xrole_context_t::runtime_stand_alone(const uint64_t timer_round, common::xaccount_address_t const & sys_addr) const {
    static std::vector<common::xaccount_address_t> sys_addr_list{common::xaccount_address_t{sys_contract_rec_elect_edge_addr},
                                                                 common::xaccount_address_t{sys_contract_rec_elect_archive_addr},
                                                                 // common::xaccount_address_t{ sys_contract_zec_elect_edge_addr },
                                                                 // common::xaccount_address_t{ sys_contract_zec_elect_archive_addr },
                                                                 common::xaccount_address_t{sys_contract_rec_elect_zec_addr},
                                                                 common::xaccount_address_t{sys_contract_zec_elect_consensus_addr}};

    if (std::find(std::begin(sys_addr_list), std::end(sys_addr_list), sys_addr) == std::end(sys_addr_list)) {
        return false;
    }

    auto account = m_store->query_account(sys_addr.value());
    return 0 == account->get_chain_height();
}

bool xrole_context_t::valid_call(const uint64_t onchain_timer_round) {
    auto iter = m_address_round_map.find(m_contract_info->address);
    if (iter == m_address_round_map.end() || (iter != m_address_round_map.end() && iter->second < onchain_timer_round)) {
        m_address_round_map[m_contract_info->address] = onchain_timer_round;
        return true;
    } else {
        xinfo("not valid call %llu", onchain_timer_round);
        return false;
    }
}

bool xrole_context_t::is_scheduled_table_contract(common::xaccount_address_t const& addr) const {
    return addr == common::xaccount_address_t{sys_contract_sharding_workload_addr} ||
        addr == common::xaccount_address_t{sys_contract_sharding_slash_info_addr};
}

void xrole_context_t::call_contract(const uint64_t onchain_timer_round, xblock_monitor_info_t * info, const uint64_t block_timestamp) {
    base::xstream_t stream(base::xcontext_t::instance());
    stream << onchain_timer_round;
    std::string action_params = std::string((char *)stream.data(), stream.size());

    call_contract(action_params, block_timestamp, info);
}

void xrole_context_t:: call_contract(const uint64_t onchain_timer_round, xblock_monitor_info_t * info, const uint64_t block_timestamp, uint16_t table_id) {
    base::xstream_t stream(base::xcontext_t::instance());
    stream << onchain_timer_round;
    std::string action_params = std::string((char *)stream.data(), stream.size());

    call_contract(action_params, block_timestamp, info, table_id);
}

bool xrole_context_t::is_timer_unorder(common::xaccount_address_t const & address, uint64_t timestamp) {
    if (address == common::xaccount_address_t{sys_contract_beacon_timer_addr}) {
        auto block = m_syncstore->get_vblockstore()->get_latest_committed_block(address.value());
        if (abs((int64_t)(((xblock_t *)block.get())->get_timestamp() - timestamp)) <= 3) {
            return true;
        }
    }
    return false;
}

void xrole_context_t::call_contract(const std::string & action_params, uint64_t timestamp, xblock_monitor_info_t * info) {
    std::vector<common::xaccount_address_t> addresses;
    if (is_sys_sharding_contract_address(m_contract_info->address)) {
        for (auto & tid : m_driver->table_ids()) {
            addresses.push_back(xcontract_address_map_t::calc_cluster_address(m_contract_info->address, tid));
        }
    } else {
        addresses.push_back(m_contract_info->address);
    }
    xproperty_asset asset_out{0};
    for (auto & address : addresses) {
        if (is_timer_unorder(address, timestamp)) {
            xinfo("[xrole_context_t] call_contract in consensus mode, address timer unorder, not create tx", address.value().c_str());
            continue;
        }
        auto tx = make_object_ptr<xtransaction_t>();

        tx->make_tx_run_contract(asset_out, info->action, action_params);
        tx->set_same_source_target_address(address.value());
        xaccount_ptr_t account = m_store->query_account(address.value());
        assert(account != nullptr);
        tx->set_last_trans_hash_and_nonce(account->account_send_trans_hash(), account->account_send_trans_number());
        tx->set_fire_timestamp(timestamp);
        tx->set_expire_duration(300);
        tx->set_digest();
        tx->set_len();

        if (info->call_way == enum_call_action_way_t::consensus) {
            int32_t r = m_unit_service->request_transaction_consensus(tx, true);
            xinfo("[xrole_context_t] call_contract in consensus mode with return code : %d, %s, %s %s %ld, %lld",
                  r,
                  tx->get_digest_hex_str().c_str(),
                  address.value().c_str(),
                  data::to_hex_str(account->account_send_trans_hash()).c_str(),
                  account->account_send_trans_number(),
                  timestamp);
        } else {
            // TODO(jimmy) now support
            xassert(false);
            // xvm::xvm_service s;
            // xaccount_context_t ac(address.value(), m_store.get());
            // auto trace = s.deal_transaction(tx, &ac);
            // xinfo("[xrole_context_t] call_contract in no_consensus mode with return code : %d", (int)trace->m_errno);
        }
    }
}

void xrole_context_t::call_contract(const std::string & action_params, uint64_t timestamp, xblock_monitor_info_t * info, uint16_t table_id) {
    auto const address = xcontract_address_map_t::calc_cluster_address(m_contract_info->address, table_id);

    if (is_timer_unorder(address, timestamp)) {
        xinfo("[xrole_context_t] call_contract in consensus mode, address timer unorder, not create tx", address.c_str());
        return;
    }
    auto tx = make_object_ptr<xtransaction_t>();
    tx->make_tx_run_contract(info->action, action_params);
    tx->set_same_source_target_address(address.value());
    xaccount_ptr_t account = m_store->query_account(address.value());
    assert(account != nullptr);
    tx->set_last_trans_hash_and_nonce(account->account_send_trans_hash(), account->account_send_trans_number());
    tx->set_fire_timestamp(timestamp);
    tx->set_expire_duration(300);
    tx->set_digest();
    tx->set_len();
    if (info->call_way == enum_call_action_way_t::consensus) {
        int32_t r = m_unit_service->request_transaction_consensus(tx, true);
        xinfo("[xrole_context_t] call_contract in consensus mode with return code : %d, %s, %s %s %ld, %lld",
              r,
              tx->get_digest_hex_str().c_str(),
              address.c_str(),
              data::to_hex_str(account->account_send_trans_hash()).c_str(),
              account->account_send_trans_number(),
              timestamp);
    } else {
        // TODO(jimmy) now support
        xassert(false);
        // xvm::xvm_service s;
        // xaccount_context_t ac(address.value(), m_store.get());
        // auto trace = s.deal_transaction(tx, &ac);
        // xinfo("[xrole_context_t] call_contract in no_consensus mode with return code : %d", (int)trace->m_errno);
    }
}

void xrole_context_t::broadcast(const xblock_ptr_t & block_ptr, common::xnode_type_t types) {
    assert(block_ptr != nullptr);
    base::xstream_t stream(base::xcontext_t::instance());
    block_ptr->full_block_serialize_to(stream);
    auto message = xmessage_t({stream.data(), stream.data() + stream.size()}, xmessage_block_broadcast_id);

    if (common::has<common::xnode_type_t::all>(types)) {
        common::xnode_address_t dest{common::xcluster_address_t{m_driver->network_id()}};
        m_driver->broadcast_to(dest, message);
        xdbg("[xrole_context_t] broadcast to ALL. block owner %s height %" PRIu64, block_ptr->get_block_owner().c_str(), block_ptr->get_height());
    } else {
        if (common::has<common::xnode_type_t::committee>(types)) {
            common::xnode_address_t dest{common::build_committee_sharding_address(m_driver->network_id())};
            m_driver->forward_broadcast_message(message, dest);
            xdbg("[xrole_context_t] broadcast to beacon. block owner %s", block_ptr->get_block_owner().c_str());
        }

        if (common::has<common::xnode_type_t::zec>(types)) {
            common::xnode_address_t dest{common::build_zec_sharding_address(m_driver->network_id())};
            if (m_driver->address().cluster_address() == dest.cluster_address()) {
                m_driver->broadcast(message);
            } else {
                m_driver->forward_broadcast_message(message, dest);
            }
            xdbg("[xrole_context_t] broadcast to zec. block owner %s", block_ptr->get_block_owner().c_str());
        }

        if (common::has<common::xnode_type_t::archive>(types)) {
            common::xnode_address_t dest{
                common::build_archive_sharding_address(m_driver->network_id()),
            };
            m_driver->forward_broadcast_message(message, dest);
            xdbg("[xrole_context_t] broadcast to archive. block owner %s", block_ptr->get_block_owner().c_str());
        }
    }
}

NS_END2
