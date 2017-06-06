/*
 * Copyright (C) 2017  CZ.NIC, z.s.p.o.
 *
 * This file is part of FRED.
 *
 * FRED is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * FRED is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FRED.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "src/getdns/solver.hh"
#include "src/getdns/data.hh"
#include "src/getdns/exception.hh"

#include <iostream>

namespace GetDns
{

Solver::Solver()
{ }

Solver::~Solver()
{
}

::getdns_transaction_t Solver::add_request_for_address_resolving(
        const std::string& _hostname,
        const RequestPtr& _request,
        const boost::optional<TransportList>& _transport_list)
{
    _request->join(event_base_);
    if (_transport_list)
    {
        _request->get_context().set_dns_transport_list(*_transport_list);
    }
    const ::getdns_transaction_t transaction_id =
            _request->get_context().add_request_for_address_resolving(_hostname, this, getdns_callback_function);
    active_requests_.insert(std::make_pair(transaction_id, _request));
    return transaction_id;
}

void Solver::do_one_step()
{
    const Event::Base::Result::Enum loop_status = event_base_.loop();
    switch (loop_status)
    {
    case Event::Base::Result::success:
    case Event::Base::Result::no_events:
        return;
    }
    struct UnexpectedResult:Exception
    {
        const char* what()const throw() { return "event_base_loop returned unexpected value"; }
    };
    throw UnexpectedResult();
}

std::size_t Solver::get_number_of_unresolved_requests()const
{
    return active_requests_.size();
}

Solver::ListOfRequestPtr Solver::pop_finished_requests()
{
    const ListOfRequestPtr result = finished_requests_;
    finished_requests_.clear();
    return result;
}

void Solver::getdns_callback_function(
        ::getdns_context*,
        ::getdns_callback_type_t _callback_type,
        ::getdns_dict* _response,
        void* _user_data_ptr,
        ::getdns_transaction_t _transaction_id)
{
    const Data::Dict answer(_response);
    Solver* const solver_instance_ptr = reinterpret_cast<Solver*>(_user_data_ptr);
    RequestId::iterator request_itr = solver_instance_ptr->active_requests_.find(_transaction_id);
    if (request_itr == solver_instance_ptr->active_requests_.end())
    {
        return;
    }

    try
    {
        switch (_callback_type)
        {
            case GETDNS_CALLBACK_CANCEL:
                request_itr->second->on_cancel(_transaction_id);
                break;
            case GETDNS_CALLBACK_TIMEOUT:
                request_itr->second->on_timeout(_transaction_id);
                break;
            case GETDNS_CALLBACK_ERROR:
                request_itr->second->on_error(_transaction_id);
                break;
            case GETDNS_CALLBACK_COMPLETE:
                request_itr->second->on_complete(answer, _transaction_id);
                break;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "std::exception catched: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "unexpected exception catched" << std::endl;
    }
    solver_instance_ptr->finished_requests_.push_back(request_itr->second);
    solver_instance_ptr->active_requests_.erase(request_itr);
}

}//namespace GetDns
