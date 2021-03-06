// Copyright (c) 2017-2018 Telos Foundation & contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "xvm_context.h"
#include "xbase/xmem.h"
#include "xbase/xcontext.h"
#include "xerror/xvm_error.h"
#include "xdata/xproperty.h"
#include "xvm/xcontract/xcontract_register.h"
#include "xvm/manager/xcontract_manager.h"

using namespace top::data;

NS_BEG2(top, xvm)
using base::xcontext_t;
using base::xstream_t;

xvm_context::xvm_context(xvm_service& vm_service, const xtransaction_ptr_t& trx, xaccount_context_t* account_context, xtransaction_trace_ptr trace_ptr)
:m_vm_service(vm_service)
, m_current_action(trx->get_target_action())
, m_contract_account(common::xaccount_address_t{ trx->get_target_addr() })
, m_exec_account(trx->get_source_addr())
, m_contract_helper(std::make_shared<xcontract_helper>(account_context, m_contract_account, m_exec_account))
, m_trace_ptr(trace_ptr) {
    m_contract_helper->set_transaction(trx);
}

void xvm_context::exec()
{
    if (m_current_action.get_action_type() == data::enum_xaction_type::xaction_type_create_contract_account) {
        publish_code();
        return;
    }

    //todo check white and black contract and action list
    auto native_contract = contract::xcontract_manager_t::instance().get_contract(m_contract_account);
    if (native_contract) {
        // due to contract now is NOT non-state, just clone for every calling
        std::unique_ptr<xcontract::xcontract_base> _contract{native_contract->clone()};
        assert(_contract != nullptr);
        if (_contract != nullptr) {
            _contract->exec(this);
        } else {
            xwarn("[xvm_context::exec] clone contract instance failed");
        }
        return;
    }

    //get account code and check the function is native
    auto native = m_vm_service.get_native_handler(m_current_action.get_action_name());
    if (native) {
        //todo check the code is validate
        (*native)(this);
        return;
    }

    //check the cache vm is exist
    shared_ptr<xengine> engine;
    string code;
    if (!m_vm_service.m_vm_cache.get(m_contract_account, engine)) {
        engine = std::make_shared<xlua_engine>();
        m_contract_helper->get_contract_code(code);
        engine->load_script(code, *this);
        m_vm_service.m_vm_cache.put(m_contract_account, engine);
    }
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    engine->process(m_contract_account, code, *this);
    m_trace_ptr->m_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
}

void xvm_context::publish_code()
{
    if (m_contract_helper->string_exist(data::XPROPERTY_CONTRACT_CODE)) {
        throw xvm_error{enum_xvm_error_code::enum_vm_code_is_exist, "code is exist"};
    }
    try {
        uint64_t tgas_limit{0};
        string code;
        xstream_t stream(xcontext_t::instance(), (uint8_t*)m_current_action.get_action_param().data(), m_current_action.get_action_param().size());
        stream >> tgas_limit;
        stream >> code;
        shared_ptr<xengine> engine = make_shared<xlua_engine>();
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        engine->publish_script(code, *this);
        m_trace_ptr->m_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        m_vm_service.m_vm_cache.put(m_contract_account, engine);
        m_contract_helper->set_contract_code(code);
        m_contract_helper->string_set(XPROPERTY_CONTRACT_TGAS_LIMIT_KEY, std::to_string(tgas_limit), true);
    } catch(const xvm_error& e) {
        throw e;
    } catch(enum_xerror_code& e) {
        throw xvm_error{enum_xvm_error_code::enum_lua_abi_input_error, "action_param stream code is not valid"};
    } catch(...) {
        throw xvm_error{enum_xvm_error_code::enum_lua_abi_input_error, "unkown exception"};
    }
}

NS_END2
