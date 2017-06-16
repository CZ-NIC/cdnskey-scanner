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

#include "src/getdns/data.hh"
#include "src/getdns/context.hh"
#include "src/getdns/solver.hh"
#include "src/getdns/exception.hh"
#include "src/getdns/error.hh"

#include <time.h>

#include <algorithm>
#include <string>
#include <set>
#include <map>
#include <list>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstring>

#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>

namespace {

typedef std::set<std::string> Nameservers;
typedef std::set<std::string> Domains;

class DomainsToScanning
{
public:
    DomainsToScanning(std::istream& _data_source);
    ~DomainsToScanning();
    std::size_t get_number_of_nameservers()const;
    std::size_t get_number_of_domains()const;
    std::size_t get_number_of_secure_domains()const;
    Nameservers get_nameservers()const;
    const Domains& get_signed_domains()const;
    Domains get_unsigned_domains_of(const std::string& _nameserver)const;
private:
    DomainsToScanning& append_data(const char* _data_chunk, std::streamsize _data_chunk_length);
    void data_finished();
    enum Section
    {
        none,
        secure,
        insecure,
    } section_;
    typedef std::map<std::string, Domains> DomainsOfNamserver;
    DomainsOfNamserver unsigned_domains_of_namserver_;
    std::string nameserver_;
    Domains signed_domains_;
    Domains unsigned_domains_;
    std::string rest_of_data_;
    bool data_starts_at_new_line_;
};

struct Seconds
{
    explicit Seconds(::int64_t sec):value(sec) { }
    ::int64_t value;
};

struct Nanoseconds
{
    explicit Nanoseconds(::int64_t nsec):value(nsec) { }
    explicit Nanoseconds(const Seconds& sec):value(1000000000LL * sec.value) { }
    ::int64_t value;
};

struct ::timespec get_clock_monotonic();

Nanoseconds operator-(const struct ::timespec& a, const struct ::timespec& b);

struct ::timespec operator+(const struct ::timespec& a, const Seconds& b);

struct ::timespec operator+(const struct ::timespec& a, const Nanoseconds& b);

struct Insecure
{
    struct Query
    {
        std::string domain;
        std::string nameserver;
    } query;
    struct Answer
    {
        boost::asio::ip::address address;
    } answer;
};

typedef std::vector<Insecure> VectorOfInsecures;

void resolve_hostnames_of_nameservers(
        const DomainsToScanning& input,
        const Seconds& timeout_sec,
        const Nanoseconds& runtime_usec,
        const boost::optional<GetDns::TransportList>& transport_list,
        const std::list<boost::asio::ip::address>& resolvers,
        VectorOfInsecures& result);

struct Cdnskey
{
    ::uint16_t flags;
    ::uint8_t protocol;
    ::uint8_t algorithm;
    std::string public_key;
    friend std::ostream& operator<<(std::ostream& out, const Cdnskey& value)
    {
        out << ::uint32_t(value.flags)
            << " " << ::uint32_t(value.protocol)
            << " " << ::uint32_t(value.algorithm)
            << " " << GetDns::Data::base64_encode(value.public_key);
        return out;
    }
};

class HostnameResolver
{
public:
    typedef std::map< std::string, std::set<boost::asio::ip::address> > Result;
    static Result get_result(
            const std::set<std::string>& _hostnames,
            const Seconds& _query_timeout_sec,
            const boost::optional<GetDns::TransportList>& _transport_list,
            const std::list<boost::asio::ip::address>& _resolvers,
            const Nanoseconds& _assigned_time_nsec);
private:
    class Query;
};

class InsecureCdnskeyResolver
{
public:
    static void resolve(
            const VectorOfInsecures& _to_resolve,
            const Seconds& _query_timeout_sec,
            const boost::optional<GetDns::TransportList>& _transport_list,
            const Nanoseconds& _assigned_time_nsec);
private:
    class Query;
};

class SecureCdnskeyResolver
{
public:
    static void resolve(
            const Domains& _to_resolve,
            const Seconds& _query_timeout_sec,
            const boost::optional<GetDns::TransportList>& _transport_list,
            const std::list<boost::asio::ip::address>& _resolvers,
            const std::list<GetDns::Data::TrustAnchor>& _trust_anchors,
            const Nanoseconds& _assigned_time_nsec);
private:
    class Query;
};

const int max_number_of_unresolved_queries = 2;

template <class T>
T split(const std::string& src, const std::string& delimiters, void(*append)(const std::string& item, T& container));

void append_ip_address(const std::string& item, std::list<boost::asio::ip::address>& addresses);
void append_trust_anchor(const std::string& item, std::list<GetDns::Data::TrustAnchor>& anchors);

extern const char cmdline_help_text[];

}//namespace {anonymous}

int main(int, char* argv[])
{
    std::string hostname_resolvers_opt;
    std::string cdnskey_resolvers_opt;
    std::string dnssec_trust_anchors_opt;
    std::string timeout_opt;
    std::string runtime_opt;
    for (char** arg_ptr = argv + 1; *arg_ptr != NULL; ++arg_ptr)
    {
        const int are_the_same = 0;
        if (std::strcmp(*arg_ptr, "--hostname_resolvers") == are_the_same)
        {
            if (!hostname_resolvers_opt.empty())
            {
                std::cerr << "hostname_resolvers option can be used once only" << std::endl;
                return EXIT_FAILURE;
            }
            ++arg_ptr;
            if (*arg_ptr == NULL)
            {
                std::cerr << "no argument for hostname_resolvers option" << std::endl;
                return EXIT_FAILURE;
            }
            hostname_resolvers_opt = *arg_ptr;
            if (hostname_resolvers_opt.empty())
            {
                std::cerr << "hostname_resolvers argument can not be empty" << std::endl;
                return EXIT_FAILURE;
            }
        }
        else if (std::strcmp(*arg_ptr, "--cdnskey_resolvers") == are_the_same)
        {
            if (!cdnskey_resolvers_opt.empty())
            {
                std::cerr << "cdnskey_resolvers option can be used once only" << std::endl;
                return EXIT_FAILURE;
            }
            ++arg_ptr;
            if (*arg_ptr == NULL)
            {
                std::cerr << "no argument for cdnskey_resolvers option" << std::endl;
                return EXIT_FAILURE;
            }
            cdnskey_resolvers_opt = *arg_ptr;
            if (cdnskey_resolvers_opt.empty())
            {
                std::cerr << "cdnskey_resolvers argument can not be empty" << std::endl;
                return EXIT_FAILURE;
            }
        }
        else if (std::strcmp(*arg_ptr, "--dnssec_trust_anchors") == are_the_same)
        {
            if (!dnssec_trust_anchors_opt.empty())
            {
                std::cerr << "dnssec_trust_anchors option can be used once only" << std::endl;
                return EXIT_FAILURE;
            }
            ++arg_ptr;
            if (*arg_ptr == NULL)
            {
                std::cerr << "no argument for dnssec_trust_anchors option" << std::endl;
                return EXIT_FAILURE;
            }
            dnssec_trust_anchors_opt = *arg_ptr;
            if (dnssec_trust_anchors_opt.empty())
            {
                std::cerr << "dnssec_trust_anchors argument can not be empty" << std::endl;
                return EXIT_FAILURE;
            }
        }
        else if (std::strcmp(*arg_ptr, "--timeout") == are_the_same)
        {
            if (!timeout_opt.empty())
            {
                std::cerr << "timeout option can be used once only" << std::endl;
                return EXIT_FAILURE;
            }
            ++arg_ptr;
            if (*arg_ptr == NULL)
            {
                std::cerr << "no argument for timeout option" << std::endl;
                return EXIT_FAILURE;
            }
            timeout_opt = *arg_ptr;
            if (timeout_opt.empty())
            {
                std::cerr << "timeout argument can not be empty" << std::endl;
                return EXIT_FAILURE;
            }
        }
        else if (std::strcmp(*arg_ptr, "--help") == are_the_same)
        {
            std::cerr << cmdline_help_text << std::endl;
            return EXIT_SUCCESS;
        }
        else
        {
            if (!runtime_opt.empty())
            {
                std::cerr << "runtime value has to be set once only" << std::endl;
                return EXIT_FAILURE;
            }
            runtime_opt = *arg_ptr;
            if (runtime_opt.empty())
            {
                std::cerr << "runtime value can not be empty" << std::endl;
                return EXIT_FAILURE;
            }
        }
    }
    if (runtime_opt.empty())
    {
        std::cerr << "runtime value has to be set" << std::endl;
        return EXIT_FAILURE;
    }
    try
    {
        const Seconds runtime(boost::lexical_cast< ::int64_t >(runtime_opt));
        if (runtime.value <= 0)
        {
            std::cerr << "lack of time" << std::endl;
            return EXIT_FAILURE;
        }
        const std::list<boost::asio::ip::address> hostname_resolvers = split(hostname_resolvers_opt, ",", append_ip_address);
        const std::list<boost::asio::ip::address> cdnskey_resolvers = split(cdnskey_resolvers_opt, ",", append_ip_address);
        const std::list<GetDns::Data::TrustAnchor> anchors = split(dnssec_trust_anchors_opt, ",", append_trust_anchor);
        const Seconds timeout_default(10);
        const Seconds query_timeout = timeout_opt.empty() ? timeout_default
                                                          : Seconds(boost::lexical_cast< ::uint64_t >(timeout_opt));
        const DomainsToScanning domains_to_scanning(std::cin);
        const struct ::timespec t_end = get_clock_monotonic() + runtime;
        GetDns::Solver solver;
        GetDns::TransportList tcp_only;
        tcp_only.push_back(GetDns::Transport::tcp);
        VectorOfInsecures insecure_queries;
        {
            const std::size_t estimated_total_number_of_queries =
                    domains_to_scanning.get_number_of_nameservers() + 2 * domains_to_scanning.get_number_of_domains();
            std::cerr << "estimated_total_number_of_queries = " << estimated_total_number_of_queries << std::endl;
            const double query_distance = double(runtime.value) / estimated_total_number_of_queries;
            std::cerr << "query_distance = " << query_distance << std::endl;
            const std::size_t queries_to_ask_now = domains_to_scanning.get_number_of_nameservers();
            std::cerr << "queries_to_ask_now = " << queries_to_ask_now << std::endl;
            const Nanoseconds time_for_hostname_resolver(query_distance * queries_to_ask_now * 1000000000LL);
            std::cerr << "time_for_hostname_resolver = " << time_for_hostname_resolver.value << "ns" << std::endl;
            resolve_hostnames_of_nameservers(
                    domains_to_scanning,
                    query_timeout,
                    time_for_hostname_resolver,
                    tcp_only,
                    hostname_resolvers,
                    insecure_queries);
        }
        const Nanoseconds time_to_the_end = t_end - get_clock_monotonic();
        if (time_to_the_end.value <= 0)
        {
            std::cerr << "lack of time" << std::endl;
            return EXIT_FAILURE;
        }
        const std::size_t number_of_insecure_queries = insecure_queries.size();
        std::cerr << "number_of_insecure_queries = " << number_of_insecure_queries << std::endl;
        const std::size_t number_of_secure_queries = domains_to_scanning.get_number_of_secure_domains();
        std::cerr << "number_of_secure_queries = " << number_of_secure_queries << std::endl;
        const std::size_t total_number_of_queries = number_of_insecure_queries + number_of_secure_queries;
        const double query_distance_nsec = double(time_to_the_end.value) / total_number_of_queries;
        std::cerr << "query_distance = " << query_distance_nsec << "ns" << std::endl;
        const Nanoseconds time_for_insecure_resolver((query_distance_nsec * number_of_insecure_queries) + 0.5);
        const Nanoseconds time_for_secure_resolver((query_distance_nsec * number_of_secure_queries) + 0.5);
        InsecureCdnskeyResolver::resolve(
                insecure_queries,
                query_timeout,
                tcp_only,
                time_for_insecure_resolver);
        SecureCdnskeyResolver::resolve(
                domains_to_scanning.get_signed_domains(),
                query_timeout,
                tcp_only,
                cdnskey_resolvers,
                anchors,
                time_for_secure_resolver);
        return EXIT_SUCCESS;
    }
    catch (const Event::Exception& e)
    {
        std::cerr << "caught Event::Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const GetDns::Error& e)
    {
        std::cerr << "caught GetDns::Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const GetDns::Exception& e)
    {
        std::cerr << "caught GetDns::Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << "caught std::exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "caught an unexpected exception" << std::endl;
        return EXIT_FAILURE;
    }
}

namespace {

DomainsToScanning::DomainsToScanning(std::istream& _data_source)
    : section_(none),
      data_starts_at_new_line_(true)
{
    while (!_data_source.eof())
    {
        const bool stdin_is_broken = !std::cin;
        if (stdin_is_broken)
        {
            throw std::runtime_error("stream is broken");
        }
        char data_chunk[0x10000];
        _data_source.read(data_chunk, sizeof(data_chunk));
        const std::streamsize data_chunk_length = _data_source.gcount();
        this->append_data(data_chunk, data_chunk_length);
    }
    this->data_finished();
}

DomainsToScanning::~DomainsToScanning()
{
}

std::size_t DomainsToScanning::get_number_of_nameservers()const
{
    return unsigned_domains_of_namserver_.size();
}

std::size_t DomainsToScanning::get_number_of_domains()const
{
    std::size_t sum_count = signed_domains_.size();
    for (DomainsOfNamserver::const_iterator itr = unsigned_domains_of_namserver_.begin();
         itr != unsigned_domains_of_namserver_.end(); ++itr)
    {
        sum_count += itr->second.size();
    }
    return sum_count;
}

std::size_t DomainsToScanning::get_number_of_secure_domains()const
{
    return signed_domains_.size();
}

const char section_of_signed_domains[] = "[secure]";
const char section_of_unsigned_domains[] = "[insecure]";

DomainsToScanning& DomainsToScanning::append_data(const char* _data_chunk, std::streamsize _data_chunk_length)
{
    const char* const data_end = _data_chunk + _data_chunk_length;
    const char* item_begin = _data_chunk;
    const char* current_pos = _data_chunk;
    while (current_pos < data_end)
    {
        static const char item_delimiter = ' ';
        static const char line_delimiter = '\n';
        const bool item_end_reached = *current_pos == item_delimiter;
        const bool line_end_reached = *current_pos == line_delimiter;
        const bool some_delimiter_reached = item_end_reached || line_end_reached;
        if (!some_delimiter_reached)
        {
            ++current_pos;
            continue;
        }
        const std::size_t item_length = current_pos - item_begin;
        const std::string item = rest_of_data_ + std::string(item_begin, item_length);
        rest_of_data_.clear();
        const bool check_section_flag = data_starts_at_new_line_ && line_end_reached;
        if (check_section_flag)
        {
            const bool section_of_signed_domains_reached = item == section_of_signed_domains;
            if (section_of_signed_domains_reached)
            {
                section_ = secure;
                nameserver_.clear();
                unsigned_domains_.clear();
                data_starts_at_new_line_ = true;
                ++current_pos;
                item_begin = current_pos;
                continue;
            }
            const bool section_of_unsigned_domains_reached = item == section_of_unsigned_domains;
            if (section_of_unsigned_domains_reached)
            {
                section_ = insecure;
                nameserver_.clear();
                unsigned_domains_.clear();
                data_starts_at_new_line_ = true;
                ++current_pos;
                item_begin = current_pos;
                continue;
            }
        }
        switch (section_)
        {
            case secure:
                signed_domains_.insert(item);
                break;
            case insecure:
            {
                const bool item_is_nameserver = data_starts_at_new_line_;
                if (item_is_nameserver)
                {
                    nameserver_ = item;
                    data_starts_at_new_line_ = false;
                    unsigned_domains_.clear();
                }
                else
                {
                    unsigned_domains_.insert(item);
                }
                break;
            }
            case none:
                throw std::runtime_error("no section specified yet");
        }
        if (line_end_reached)
        {
            const bool nameserver_data_available = (section_ == insecure) && !nameserver_.empty() && !unsigned_domains_.empty();
            if (nameserver_data_available)
            {
                unsigned_domains_of_namserver_.insert(std::make_pair(nameserver_, unsigned_domains_));
            }
            nameserver_.clear();
            unsigned_domains_.clear();
            data_starts_at_new_line_ = true;
        }
        ++current_pos;
        item_begin = current_pos;
    }
    const std::size_t rest_of_data_length = current_pos - item_begin;
    rest_of_data_.append(item_begin, rest_of_data_length);
    return *this;
}

void DomainsToScanning::data_finished()
{
    const std::string item = rest_of_data_;
    const bool check_section_flag = data_starts_at_new_line_;
    if (check_section_flag)
    {
        const bool section_of_signed_domains_reached = item == section_of_signed_domains;
        if (section_of_signed_domains_reached)
        {
            return;
        }
        const bool section_of_unsigned_domains_reached = item == section_of_unsigned_domains;
        if (section_of_unsigned_domains_reached)
        {
            return;
        }
    }
    switch (section_)
    {
        case secure:
            signed_domains_.insert(item);
            return;
        case insecure:
        {
            const bool item_is_nameserver = data_starts_at_new_line_;
            if (!item_is_nameserver)
            {
                unsigned_domains_.insert(item);
                const bool nameserver_data_available = !nameserver_.empty() && !unsigned_domains_.empty();
                if (nameserver_data_available)
                {
                    unsigned_domains_of_namserver_.insert(std::make_pair(nameserver_, unsigned_domains_));
                }
                nameserver_.clear();
                unsigned_domains_.clear();
            }
            return;
        }
        case none:
            throw std::runtime_error("no section specified yet");
    }
}

struct DomainNameserver
{
    friend bool operator<(const DomainNameserver& a, const DomainNameserver& b)
    {
        return a.domain < b.domain;
    }
    std::string domain;
    std::string nameserver;
};

void resolve_hostnames_of_nameservers(
        const DomainsToScanning& input,
        const Seconds& query_timeout,
        const Nanoseconds& runtime_nsec,
        const boost::optional<GetDns::TransportList>& transport_list,
        const std::list<boost::asio::ip::address>& resolvers,
        VectorOfInsecures& result)
{
    typedef std::set<boost::asio::ip::address> IpAddresses;
    typedef std::map<std::string, IpAddresses> IpAddressesOfNameservers;
    typedef std::map< boost::asio::ip::address, std::set<DomainNameserver> > IpAddressesToDomainNameserver;
//    GetDns::Extensions extensions;
    const Nameservers nameservers = input.get_nameservers();
    const HostnameResolver::Result nameserver_addresses = HostnameResolver::get_result(
            nameservers,
            query_timeout,
            transport_list,
            resolvers,
            runtime_nsec);

    IpAddressesToDomainNameserver domains_by_nameserver_addresses;
    for (IpAddressesOfNameservers::const_iterator nameserver_itr = nameserver_addresses.begin();
         nameserver_itr != nameserver_addresses.end(); ++nameserver_itr)
    {
        const std::string nameserver = nameserver_itr->first;
        for (IpAddresses::const_iterator address_itr = nameserver_itr->second.begin();
             address_itr != nameserver_itr->second.end(); ++address_itr)
        {
            const Domains domains = input.get_unsigned_domains_of(nameserver);
            DomainNameserver item;
            item.nameserver = nameserver;
            std::set<DomainNameserver>& items = domains_by_nameserver_addresses[*address_itr];
            for (Domains::const_iterator domain_itr = domains.begin(); domain_itr != domains.end(); ++domain_itr)
            {
                item.domain = *domain_itr;
                items.insert(item);
            }
        }
    }

    std::size_t number_of_items = 0;
    for (IpAddressesToDomainNameserver::const_iterator address_itr = domains_by_nameserver_addresses.begin();
         address_itr != domains_by_nameserver_addresses.end(); ++address_itr)
    {
        number_of_items += address_itr->second.size();
    }
    result.clear();
    result.reserve(number_of_items);
    for (IpAddressesToDomainNameserver::const_iterator address_itr = domains_by_nameserver_addresses.begin();
         address_itr != domains_by_nameserver_addresses.end(); ++address_itr)
    {
        Insecure item;
        item.answer.address = address_itr->first;
        for (std::set<DomainNameserver>::const_iterator domain_itr = address_itr->second.begin();
             domain_itr != address_itr->second.end(); ++domain_itr)
        {
            item.query.domain = domain_itr->domain;
            item.query.nameserver = domain_itr->nameserver;
            result.push_back(item);
        }
    }
    std::random_shuffle(result.begin(), result.end());
}

Nameservers DomainsToScanning::get_nameservers()const
{
    Nameservers nameservers;
    for (DomainsOfNamserver::const_iterator nameserver_itr = unsigned_domains_of_namserver_.begin();
         nameserver_itr != unsigned_domains_of_namserver_.end(); ++nameserver_itr)
    {
        nameservers.insert(nameserver_itr->first);
    }
    return nameservers;
}

const Domains& DomainsToScanning::get_signed_domains()const
{
    return signed_domains_;
}

Domains DomainsToScanning::get_unsigned_domains_of(const std::string& _nameserver)const
{
    DomainsOfNamserver::const_iterator nameserver_itr = unsigned_domains_of_namserver_.find(_nameserver);
    const bool nameserver_found = nameserver_itr != unsigned_domains_of_namserver_.end();
    if (nameserver_found)
    {
        return nameserver_itr->second;
    }
    const Domains no_content;
    return no_content;
}

class HostnameResolver::Query:public GetDns::Request
{
public:
    Query(const Seconds& _timeout_sec,
          const boost::optional<GetDns::TransportList>& _transport_list,
          const std::list<boost::asio::ip::address>& _resolvers)
        : timeout_sec_(_timeout_sec),
          transport_list_(_transport_list),
          resolvers_(_resolvers),
          context_ptr_(NULL),
          status_(Status::none)
    { }
    ~Query()
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
    }
    struct Status
    {
        enum Enum
        {
            none,
            in_progress,
            completed,
            cancelled,
            timed_out,
            failed
        };
    };
    Status::Enum get_status()const
    {
        return status_;
    }
    typedef std::set<boost::asio::ip::address> Result;
    const Result& get_result()const
    {
        if (this->get_status() == Status::completed)
        {
            return result_;
        }
        struct NoResultAvailable:std::runtime_error
        {
            NoResultAvailable():std::runtime_error("Request is not completed yet") { }
        };
        throw NoResultAvailable();
    }
    ::getdns_transaction_t get_request_id()const
    {
        if (request_id_)
        {
            return *request_id_;
        }
        throw std::runtime_error("request_id not set yet");
    }
private:
    GetDns::Context& get_context()
    {
        if (context_ptr_ != NULL)
        {
            return *context_ptr_;
        }
        struct NullDereferenceException:std::runtime_error
        {
            NullDereferenceException():std::runtime_error("Dereferenced context_ptr_ is NULL") { }
        };
        throw NullDereferenceException();
    }
    void join(Event::Base& _event_base)
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
        context_ptr_ = new GetDns::Context(_event_base, GetDns::Context::InitialSettings::from_os);
        if (transport_list_)
        {
            context_ptr_->set_dns_transport_list(*transport_list_);
        }
        if (!resolvers_.empty())
        {
            context_ptr_->set_upstream_recursive_servers(resolvers_);
        }
        context_ptr_->set_timeout(timeout_sec_.value * 1000);
        status_ = Status::in_progress;
    }
    void on_complete(const GetDns::Data::Dict& _answer, ::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::completed;
        result_.clear();
        const GetDns::Data::Value just_address_answers = GetDns::Data::get<GetDns::Data::List>(_answer, "just_address_answers");
        if (!GetDns::Data::Is(just_address_answers).of<GetDns::Data::List>().type)
        {
            return;
        }
        const GetDns::Data::List addresses = GetDns::Data::From(just_address_answers).get_value_of<GetDns::Data::List>();
        const std::size_t number_of_addresses = addresses.get_number_of_items();
        for (std::size_t idx = 0; idx < number_of_addresses; ++idx)
        {
            const GetDns::Data::Value address_item = GetDns::Data::get<GetDns::Data::Dict>(addresses, idx);
            if (!GetDns::Data::Is(address_item).of<GetDns::Data::Dict>().type)
            {
                continue;
            }
            const GetDns::Data::Dict address = GetDns::Data::From(address_item).get_value_of<GetDns::Data::Dict>();
            const GetDns::Data::Value address_data = GetDns::Data::get<boost::asio::ip::address>(address, "address_data");
            if (!GetDns::Data::Is(address_data).of<boost::asio::ip::address>().type)
            {
                continue;
            }
            result_.insert(GetDns::Data::From(address_data).get_value_of<boost::asio::ip::address>());
        }
    }
    void on_cancel(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::cancelled;
    }
    void on_timeout(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::timed_out;
    }
    void on_error(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::failed;
    }
    const Seconds timeout_sec_;
    const boost::optional<GetDns::TransportList> transport_list_;
    const std::list<boost::asio::ip::address> resolvers_;
    GetDns::Context* context_ptr_;
    Status::Enum status_;
    Result result_;
    boost::optional< ::getdns_transaction_t > request_id_;
};

struct ::timespec get_clock_monotonic()
{
    struct ::timespec t;
    const int retval = ::clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    const int success = 0;
    if (retval == success)
    {
        return t;
    }
    const int c_errno = errno;
    struct ClockGetTimeFailure:std::runtime_error
    {
        ClockGetTimeFailure(const char* _desc):std::runtime_error(_desc) { }
    };
    throw ClockGetTimeFailure(std::strerror(c_errno));
}

Nanoseconds operator-(const struct ::timespec& a, const struct ::timespec& b)
{
    return Nanoseconds((1000000000LL * (a.tv_sec - b.tv_sec)) + a.tv_nsec - b.tv_nsec);
}

struct ::timespec operator+(const struct ::timespec& a, const Seconds& b)
{
    struct ::timespec sum;
    sum.tv_sec = a.tv_sec + b.value;
    sum.tv_nsec = a.tv_nsec;
    return sum;
}

struct ::timespec operator+(const struct ::timespec& a, const Nanoseconds& b)
{
    struct ::timespec sum;
    sum.tv_sec = a.tv_sec + (b.value / 1000000000LL);
    sum.tv_nsec = a.tv_nsec + (b.value % 1000000000LL);
    if (1000000000LL <= sum.tv_nsec)
    {
        sum.tv_sec += 1;
        sum.tv_nsec -= 1000000000LL;
    }
    return sum;
}

HostnameResolver::Result HostnameResolver::get_result(
        const std::set<std::string>& _hostnames,
        const Seconds& _query_timeout_sec,
        const boost::optional<GetDns::TransportList>& _transport_list,
        const std::list<boost::asio::ip::address>& _resolvers,
        const Nanoseconds& _assigned_time_nsec)
{
    if (_hostnames.empty())
    {
        return Result();
    }
    GetDns::Solver solver;
    class Timer:public Event::Timeout
    {
    public:
        Timer(GetDns::Solver& _solver,
              const std::set<std::string>& _hostnames,
              const Seconds& _query_timeout_sec,
              const boost::optional<GetDns::TransportList>& _transport_list,
              const std::list<boost::asio::ip::address>& _resolvers,
              const Nanoseconds& _assigned_time_nsec)
            : Event::Timeout(_solver.get_event_base()),
              solver_(_solver),
              hostnames_(_hostnames),
              hostname_ptr_(_hostnames.begin()),
              remaining_queries_(_hostnames.size()),
              query_timeout_sec_(_query_timeout_sec),
              transport_list_(_transport_list),
              resolvers_(_resolvers),
              time_end_(get_clock_monotonic() + _assigned_time_nsec)
        {
            this->Timeout::set(0);
            while (0 < (remaining_queries_ + solver_.get_number_of_unresolved_requests()))
            {
                solver_.do_one_step();
                const GetDns::Solver::ListOfRequestPtr finished_requests = solver_.pop_finished_requests();
                for (GetDns::Solver::ListOfRequestPtr::const_iterator request_ptr_itr = finished_requests.begin();
                     request_ptr_itr != finished_requests.end(); ++request_ptr_itr)
                {
                    const GetDns::Request* const request_ptr = request_ptr_itr->get();
                    const Query* const query_ptr = dynamic_cast<const Query*>(request_ptr);
                    if (query_ptr != NULL)
                    {
                        const std::string nameserver = tasks_[query_ptr->get_request_id()];
                        if (query_ptr->get_status() == Query::Status::completed)
                        {
                            const Query::Result addresses = query_ptr->get_result();
                            result_.insert(std::make_pair(nameserver, addresses));
                        }
                        tasks_.erase(query_ptr->get_request_id());
                    }
                }
            }
        }
        ~Timer() { }
        const Result& get_result()const { return result_; }
    private:
        Event::OnTimeout& on_timeout_occurrence()
        {
            const ::getdns_transaction_t task_id = solver_.add_request_for_address_resolving(
                    *hostname_ptr_,
                    GetDns::RequestPtr(new Query(query_timeout_sec_, transport_list_, resolvers_)),
                    extensions_);
            tasks_.insert(std::make_pair(task_id, *hostname_ptr_));
            this->set_time_of_next_query();
            ++hostname_ptr_;
            --remaining_queries_;
            return *this;
        }
        Timer& set_time_of_next_query()
        {
            const struct ::timespec now = get_clock_monotonic();
            const Nanoseconds remaining_time_nsec = time_end_ - now;
            if (remaining_time_nsec.value <= 0)
            {
                this->Timeout::set(0);
            }
            else
            {
                const ::uint64_t the_one_query_time_usec = remaining_time_nsec.value / (1000 * remaining_queries_);
                this->Timeout::set(the_one_query_time_usec);
            }
            return *this;
        }
        GetDns::Solver& solver_;
        const std::set<std::string>& hostnames_;
        std::set<std::string>::const_iterator hostname_ptr_;
        std::size_t remaining_queries_;
        const Seconds query_timeout_sec_;
        const boost::optional<GetDns::TransportList>& transport_list_;
        const std::list<boost::asio::ip::address>& resolvers_;
        const struct ::timespec time_end_;
        Result result_;
        GetDns::Extensions extensions_;
        typedef std::map< ::getdns_transaction_t, std::string > Tasks;
        Tasks tasks_;
    } timer(solver,
            _hostnames,
            _query_timeout_sec,
            _transport_list,
            _resolvers,
            _assigned_time_nsec);
    return timer.get_result();
}

class InsecureCdnskeyResolver::Query:public GetDns::Request
{
public:
    Query(const Seconds& _timeout_sec,
          const boost::optional<GetDns::TransportList>& _transport_list,
          const boost::asio::ip::address& _nameserver)
        : timeout_sec_(_timeout_sec),
          transport_list_(_transport_list),
          nameserver_(_nameserver),
          context_ptr_(NULL),
          status_(Status::none)
    { }
    ~Query()
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
    }
    struct Status
    {
        enum Enum
        {
            none,
            in_progress,
            completed,
            cancelled,
            timed_out,
            failed
        };
    };
    Status::Enum get_status()const
    {
        return status_;
    }
    typedef std::vector<Cdnskey> Result;
    const Result& get_result()const
    {
        if (this->get_status() == Status::completed)
        {
            return result_;
        }
        struct NoResultAvailable:std::runtime_error
        {
            NoResultAvailable():std::runtime_error("Request is not completed yet") { }
        };
        throw NoResultAvailable();
    }
    ::getdns_transaction_t get_request_id()const
    {
        if (request_id_)
        {
            return *request_id_;
        }
        throw std::runtime_error("request_id not set yet");
    }
private:
    GetDns::Context& get_context()
    {
        if (context_ptr_ != NULL)
        {
            return *context_ptr_;
        }
        struct NullDereferenceException:std::runtime_error
        {
            NullDereferenceException():std::runtime_error("Dereferenced context_ptr_ is NULL") { }
        };
        throw NullDereferenceException();
    }
    void join(Event::Base& _event_base)
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
        context_ptr_ = new GetDns::Context(_event_base, GetDns::Context::InitialSettings::none);
        context_ptr_->set_timeout(timeout_sec_.value * 1000);
        if (transport_list_)
        {
            context_ptr_->set_dns_transport_list(*transport_list_);
        }
        std::list<boost::asio::ip::address> nameservers;
        nameservers.push_back(nameserver_);
        context_ptr_->set_upstream_recursive_servers(nameservers);
        status_ = Status::in_progress;
    }
    void on_complete(const GetDns::Data::Dict& _answer, ::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
//        std::cerr << _answer << std::endl;
        status_ = Status::completed;
        result_.clear();
        const GetDns::Data::Value replies_tree = GetDns::Data::get<GetDns::Data::List>(_answer, "replies_tree");
        if (!GetDns::Data::Is(replies_tree).of<GetDns::Data::List>().type)
        {
            return;
        }
        const GetDns::Data::List replies = GetDns::Data::From(replies_tree).get_value_of<GetDns::Data::List>();
        for (std::size_t reply_idx = 0; reply_idx < replies.get_number_of_items(); ++reply_idx)
        {
            const GetDns::Data::Value reply_value = GetDns::Data::get<GetDns::Data::Dict>(replies, reply_idx);
            if (!GetDns::Data::Is(reply_value).of<GetDns::Data::Dict>().type)
            {
                continue;
            }
            const GetDns::Data::Dict reply = GetDns::Data::From(reply_value).get_value_of<GetDns::Data::Dict>();
    //        std::cout << "reply[" << reply_idx << "]:\n" << reply << std::endl;
            const GetDns::Data::Value answer_value = GetDns::Data::get<GetDns::Data::List>(reply, "answer");
            if (!GetDns::Data::Is(answer_value).of<GetDns::Data::List>().type)
            {
                continue;
            }
            const GetDns::Data::List answers = GetDns::Data::From(answer_value).get_value_of<GetDns::Data::List>();
            for (std::size_t answer_idx = 0; answer_idx < answers.get_number_of_items(); ++answer_idx)
            {
                const GetDns::Data::Value answer_value = GetDns::Data::get<GetDns::Data::Dict>(answers, answer_idx);
                if (!GetDns::Data::Is(answer_value).of<GetDns::Data::Dict>().type)
                {
                    continue;
                }
                const GetDns::Data::Dict answer = GetDns::Data::From(answer_value).get_value_of<GetDns::Data::Dict>();
                const GetDns::Data::Value rdata_value = GetDns::Data::get<GetDns::Data::Dict>(answer, "rdata");
                if (!GetDns::Data::Is(rdata_value).of<GetDns::Data::Dict>().type)
                {
                    continue;
                }
                const GetDns::Data::Dict rdata = GetDns::Data::From(rdata_value).get_value_of<GetDns::Data::Dict>();

                Cdnskey cdnskey;
                {
                    const GetDns::Data::Value algorithm_value = GetDns::Data::get< ::uint32_t >(rdata, "algorithm");
                    if (!GetDns::Data::Is(algorithm_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.algorithm = GetDns::Data::From(algorithm_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value flags_value = GetDns::Data::get< ::uint32_t >(rdata, "flags");
                    if (!GetDns::Data::Is(flags_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.flags = GetDns::Data::From(flags_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value protocol_value = GetDns::Data::get< ::uint32_t >(rdata, "protocol");
                    if (!GetDns::Data::Is(protocol_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.protocol = GetDns::Data::From(protocol_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value public_key_value = GetDns::Data::get<std::string>(rdata, "public_key");
                    if (!GetDns::Data::Is(public_key_value).of<std::string>().type)
                    {
                        continue;
                    }
                    cdnskey.public_key = GetDns::Data::From(public_key_value).get_value_of<std::string>();
                }
                result_.push_back(cdnskey);
            }
        }
    }
    void on_cancel(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::cancelled;
    }
    void on_timeout(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::timed_out;
    }
    void on_error(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::failed;
    }
    const Seconds timeout_sec_;
    const boost::optional<GetDns::TransportList> transport_list_;
    const boost::asio::ip::address nameserver_;
    GetDns::Context* context_ptr_;
    Status::Enum status_;
    Result result_;
    boost::optional< ::getdns_transaction_t > request_id_;
};

void InsecureCdnskeyResolver::resolve(
        const VectorOfInsecures& _to_resolve,
        const Seconds& _query_timeout_sec,
        const boost::optional<GetDns::TransportList>& _transport_list,
        const Nanoseconds& _assigned_time_nsec)
{
    if (_to_resolve.empty())
    {
        return;
    }
    GetDns::Solver solver;
    class Timer:public Event::Timeout
    {
    public:
        Timer(GetDns::Solver& _solver,
              const VectorOfInsecures& _to_resolve,
              const Seconds& _query_timeout_sec,
              const boost::optional<GetDns::TransportList>& _transport_list,
              const Nanoseconds& _assigned_time_nsec)
            : Event::Timeout(_solver.get_event_base()),
              solver_(_solver),
              to_resolve_(_to_resolve),
              item_to_resolve_ptr_(_to_resolve.begin()),
              remaining_queries_(_to_resolve.size()),
              query_timeout_sec_(_query_timeout_sec),
              transport_list_(_transport_list),
              time_end_(get_clock_monotonic() + _assigned_time_nsec)
        {
            this->Timeout::set(0);
            while (0 < (remaining_queries_ + solver_.get_number_of_unresolved_requests()))
            {
                _solver.do_one_step();
                const GetDns::Solver::ListOfRequestPtr finished_requests = solver_.pop_finished_requests();
                for (GetDns::Solver::ListOfRequestPtr::const_iterator request_ptr_itr = finished_requests.begin();
                     request_ptr_itr != finished_requests.end(); ++request_ptr_itr)
                {
                    const GetDns::Request* const request_ptr = request_ptr_itr->get();
                    const Query* const query_ptr = dynamic_cast<const Query*>(request_ptr);
                    if (query_ptr != NULL)
                    {
                        const Insecure to_resolve = tasks_[query_ptr->get_request_id()];
                        if (query_ptr->get_status() == Query::Status::completed)
                        {
                            const Query::Result result = query_ptr->get_result();
                            if (result.empty())
                            {
                                std::cout << "insecure-empty " << to_resolve.query.nameserver << " "
                                          << to_resolve.answer.address << " "
                                          << to_resolve.query.domain << std::endl;
                            }
                            else
                            {
                                for (Query::Result::const_iterator key_itr = result.begin(); key_itr != result.end(); ++key_itr)
                                {
                                    std::cout << "insecure " << to_resolve.query.nameserver << " "
                                              << to_resolve.answer.address << " "
                                              << to_resolve.query.domain << " "
                                              << *key_itr << std::endl;
                                }
                            }
                        }
                        else
                        {
                            std::cout << "unresolved " << to_resolve.query.nameserver << " "
                                      << to_resolve.answer.address << " "
                                      << to_resolve.query.domain << std::endl;
                        }
                        tasks_.erase(query_ptr->get_request_id());
                    }
                }
            }
        }
        ~Timer() { }
    private:
        Event::OnTimeout& on_timeout_occurrence()
        {
            const ::getdns_transaction_t task_id = solver_.add_request_for_cdnskey_resolving(
                    item_to_resolve_ptr_->query.domain,
                    GetDns::RequestPtr(new Query(query_timeout_sec_, transport_list_, item_to_resolve_ptr_->answer.address)),
                    extensions_);
            tasks_.insert(std::make_pair(task_id, *item_to_resolve_ptr_));
            this->set_time_of_next_query();
            ++item_to_resolve_ptr_;
            --remaining_queries_;
            return *this;
        }
        Timer& set_time_of_next_query()
        {
            const struct ::timespec now = get_clock_monotonic();
            const Nanoseconds remaining_time_nsec = time_end_ - now;
            if (remaining_time_nsec.value <= 0)
            {
                this->Timeout::set(0);
            }
            else
            {
                const ::uint64_t the_one_query_time_usec = remaining_time_nsec.value / (1000 * remaining_queries_);
                this->Timeout::set(the_one_query_time_usec);
            }
            return *this;
        }
        GetDns::Solver& solver_;
        const VectorOfInsecures& to_resolve_;
        VectorOfInsecures::const_iterator item_to_resolve_ptr_;
        std::size_t remaining_queries_;
        const Seconds query_timeout_sec_;
        const boost::optional<GetDns::TransportList>& transport_list_;
        const struct ::timespec time_end_;
        GetDns::Extensions extensions_;
        typedef std::map< ::getdns_transaction_t, Insecure > Tasks;
        Tasks tasks_;
    } timer(solver,
            _to_resolve,
            _query_timeout_sec,
            _transport_list,
            _assigned_time_nsec);
}

class SecureCdnskeyResolver::Query:public GetDns::Request
{
public:
    Query(const Seconds& _timeout_sec,
          const boost::optional<GetDns::TransportList>& _transport_list,
          const std::list<boost::asio::ip::address>& _resolvers,
          const std::list<GetDns::Data::TrustAnchor>& _trust_anchors)
        : timeout_sec_(_timeout_sec),
          transport_list_(_transport_list),
          resolvers_(_resolvers),
          trust_anchors_(_trust_anchors),
          context_ptr_(NULL),
          status_(Status::none)
    { }
    ~Query()
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
    }
    struct Status
    {
        enum Enum
        {
            none,
            in_progress,
            completed,
            untrustworthy_answer,
            cancelled,
            timed_out,
            failed
        };
    };
    Status::Enum get_status()const
    {
        return status_;
    }
    struct Result
    {
        struct Trustiness
        {
            enum Enum
            {
                insecure,
                secure,
                bogus
            };
            friend std::ostream& operator<<(std::ostream& out, Enum value)
            {
                switch (value)
                {
                    case insecure: return out << "insecure";
                    case secure: return out << "secure";
                    case bogus: return out << "bogus";
                }
                return out << "unknown";
            }
        };
        Trustiness::Enum trustiness;
        std::vector<Cdnskey> cdnskeys;
    };
    const Result& get_result()const
    {
        if (this->get_status() == Status::completed)
        {
            return result_;
        }
        struct NoResultAvailable:std::runtime_error
        {
            NoResultAvailable():std::runtime_error("Request is not completed yet") { }
        };
        throw NoResultAvailable();
    }
    ::getdns_transaction_t get_request_id()const
    {
        if (request_id_)
        {
            return *request_id_;
        }
        throw std::runtime_error("request_id not set yet");
    }
private:
    GetDns::Context& get_context()
    {
        if (context_ptr_ != NULL)
        {
            return *context_ptr_;
        }
        struct NullDereferenceException:std::runtime_error
        {
            NullDereferenceException():std::runtime_error("Dereferenced context_ptr_ is NULL") { }
        };
        throw NullDereferenceException();
    }
    void join(Event::Base& _event_base)
    {
        if (context_ptr_ != NULL)
        {
            delete context_ptr_;
            context_ptr_ = NULL;
        }
        context_ptr_ = new GetDns::Context(_event_base, GetDns::Context::InitialSettings::from_os);
        if (!resolvers_.empty())
        {
            context_ptr_->set_upstream_recursive_servers(resolvers_);
        }
        context_ptr_->set_timeout(timeout_sec_.value * 1000);
        if (transport_list_)
        {
            context_ptr_->set_dns_transport_list(*transport_list_);
        }
        if (!trust_anchors_.empty())
        {
            context_ptr_->set_dnssec_trust_anchors(trust_anchors_);
        }
        status_ = Status::in_progress;
    }
    void on_complete(const GetDns::Data::Dict& _answer, ::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
    //    std::cout << _answer << std::endl;
        status_ = Status::untrustworthy_answer;
        result_.cdnskeys.clear();
        const GetDns::Data::Value replies_tree = GetDns::Data::get<GetDns::Data::List>(_answer, "replies_tree");
        if (!GetDns::Data::Is(replies_tree).of<GetDns::Data::List>().type)
        {
            return;
        }
        const GetDns::Data::List replies = GetDns::Data::From(replies_tree).get_value_of<GetDns::Data::List>();
        for (std::size_t reply_idx = 0; reply_idx < replies.get_number_of_items(); ++reply_idx)
        {
            const GetDns::Data::Value reply_value = GetDns::Data::get<GetDns::Data::Dict>(replies, reply_idx);
            if (!GetDns::Data::Is(reply_value).of<GetDns::Data::Dict>().type)
            {
                continue;
            }
            const GetDns::Data::Dict reply = GetDns::Data::From(reply_value).get_value_of<GetDns::Data::Dict>();
    //        std::cout << "reply[" << reply_idx << "]:\n" << reply << std::endl;
            const GetDns::Data::Value answer_value = GetDns::Data::get<GetDns::Data::List>(reply, "answer");
            if (!GetDns::Data::Is(answer_value).of<GetDns::Data::List>().type)
            {
                continue;
            }
            const GetDns::Data::List answers = GetDns::Data::From(answer_value).get_value_of<GetDns::Data::List>();
            for (std::size_t answer_idx = 0; answer_idx < answers.get_number_of_items(); ++answer_idx)
            {
                const GetDns::Data::Value answer_value = GetDns::Data::get<GetDns::Data::Dict>(answers, answer_idx);
                if (!GetDns::Data::Is(answer_value).of<GetDns::Data::Dict>().type)
                {
                    continue;
                }
                const GetDns::Data::Dict answer = GetDns::Data::From(answer_value).get_value_of<GetDns::Data::Dict>();
                const GetDns::Data::Value rdata_value = GetDns::Data::get<GetDns::Data::Dict>(answer, "rdata");
                if (!GetDns::Data::Is(rdata_value).of<GetDns::Data::Dict>().type)
                {
                    continue;
                }
                const GetDns::Data::Dict rdata = GetDns::Data::From(rdata_value).get_value_of<GetDns::Data::Dict>();

                Cdnskey cdnskey;
                {
                    const GetDns::Data::Value algorithm_value = GetDns::Data::get< ::uint32_t >(rdata, "algorithm");
                    if (!GetDns::Data::Is(algorithm_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.algorithm = GetDns::Data::From(algorithm_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value flags_value = GetDns::Data::get< ::uint32_t >(rdata, "flags");
                    if (!GetDns::Data::Is(flags_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.flags = GetDns::Data::From(flags_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value protocol_value = GetDns::Data::get< ::uint32_t >(rdata, "protocol");
                    if (!GetDns::Data::Is(protocol_value).of< ::uint32_t >().type)
                    {
                        continue;
                    }
                    cdnskey.protocol = GetDns::Data::From(protocol_value).get_value_of< ::uint32_t >();
                }
                {
                    const GetDns::Data::Value public_key_value = GetDns::Data::get<std::string>(rdata, "public_key");
                    if (!GetDns::Data::Is(public_key_value).of<std::string>().type)
                    {
                        continue;
                    }
                    cdnskey.public_key = GetDns::Data::From(public_key_value).get_value_of<std::string>();
                }
                result_.cdnskeys.push_back(cdnskey);
            }
        }
        status_ = Status::completed;
    }
    void on_cancel(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::cancelled;
    }
    void on_timeout(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::timed_out;
    }
    void on_error(::getdns_transaction_t _request_id)
    {
        request_id_ = _request_id;
        status_ = Status::failed;
    }
    const Seconds timeout_sec_;
    const boost::optional<GetDns::TransportList> transport_list_;
    const std::list<boost::asio::ip::address> resolvers_;
    const std::list<GetDns::Data::TrustAnchor> trust_anchors_;
    GetDns::Context* context_ptr_;
    Status::Enum status_;
    Result result_;
    boost::optional< ::getdns_transaction_t > request_id_;
};

void SecureCdnskeyResolver::resolve(
        const Domains& _to_resolve,
        const Seconds& _query_timeout_sec,
        const boost::optional<GetDns::TransportList>& _transport_list,
        const std::list<boost::asio::ip::address>& _resolvers,
        const std::list<GetDns::Data::TrustAnchor>& _trust_anchors,
        const Nanoseconds& _assigned_time_nsec)
{
    if (_to_resolve.empty())
    {
        return;
    }
    GetDns::Solver solver;
    class Timer:public Event::Timeout
    {
    public:
        Timer(GetDns::Solver& _solver,
              const Domains& _to_resolve,
              const Seconds& _query_timeout_sec,
              const boost::optional<GetDns::TransportList>& _transport_list,
              const std::list<boost::asio::ip::address>& _resolvers,
              const std::list<GetDns::Data::TrustAnchor>& _trust_anchors,
              const Nanoseconds& _assigned_time_nsec)
            : Event::Timeout(_solver.get_event_base()),
              solver_(_solver),
              to_resolve_(_to_resolve),
              item_to_resolve_ptr_(_to_resolve.begin()),
              remaining_queries_(_to_resolve.size()),
              query_timeout_sec_(_query_timeout_sec),
              transport_list_(_transport_list),
              resolvers_(_resolvers),
              trust_anchors_(_trust_anchors),
              time_end_(get_clock_monotonic() + _assigned_time_nsec)
        {
            this->Timeout::set(0);
            extensions_.dnssec_return_only_secure = true;
            while (0 < (remaining_queries_ + solver_.get_number_of_unresolved_requests()))
            {
                solver_.do_one_step();
                const GetDns::Solver::ListOfRequestPtr finished_requests = _solver.pop_finished_requests();
                for (GetDns::Solver::ListOfRequestPtr::const_iterator request_ptr_itr = finished_requests.begin();
                     request_ptr_itr != finished_requests.end(); ++request_ptr_itr)
                {
                    const GetDns::Request* const request_ptr = request_ptr_itr->get();
                    const Query* const query_ptr = dynamic_cast<const Query*>(request_ptr);
                    if (query_ptr != NULL)
                    {
                        const std::string to_resolve = tasks_[query_ptr->get_request_id()];
                        switch (query_ptr->get_status())
                        {
                            case Query::Status::completed:
                            {
                                const Query::Result result = query_ptr->get_result();
                                if (result.cdnskeys.empty())
                                {
                                    std::cout << "secure-empty " << to_resolve << std::endl;
                                }
                                else
                                {
                                    for (std::vector<Cdnskey>::const_iterator key_itr = result.cdnskeys.begin();
                                         key_itr != result.cdnskeys.end(); ++key_itr)
                                    {
                                        std::cout << "secure " << to_resolve << " " << *key_itr << std::endl;
                                    }
                                }
                                break;
                            }
                            case Query::Status::untrustworthy_answer:
                            {
                                std::cout << "untrustworthy " << to_resolve << std::endl;
                                break;
                            }
                            case Query::Status::cancelled:
                            case Query::Status::failed:
                            case Query::Status::none:
                            case Query::Status::in_progress:
                            case Query::Status::timed_out:
                            {
                                std::cout << "unknown " << to_resolve << std::endl;
                                break;
                            }
                        }
                        tasks_.erase(query_ptr->get_request_id());
                    }
                }
            }
        }
        ~Timer() { }
    private:
        Event::OnTimeout& on_timeout_occurrence()
        {
            const ::getdns_transaction_t task_id = solver_.add_request_for_cdnskey_resolving(
                    *item_to_resolve_ptr_,
                    GetDns::RequestPtr(new Query(
                            query_timeout_sec_,
                            transport_list_,
                            resolvers_,
                            trust_anchors_)),
                    extensions_);
            tasks_.insert(std::make_pair(task_id, *item_to_resolve_ptr_));
            this->set_time_of_next_query();
            ++item_to_resolve_ptr_;
            --remaining_queries_;
            return *this;
        }
        Timer& set_time_of_next_query()
        {
            const struct ::timespec now = get_clock_monotonic();
            const Nanoseconds remaining_time_nsec = time_end_ - now;
            if (remaining_time_nsec.value <= 0)
            {
                this->Timeout::set(0);
            }
            else
            {
                const ::uint64_t the_one_query_time_usec = remaining_time_nsec.value / (1000 * remaining_queries_);
                this->Timeout::set(the_one_query_time_usec);
            }
            return *this;
        }
        GetDns::Solver& solver_;
        const Domains& to_resolve_;
        Domains::const_iterator item_to_resolve_ptr_;
        std::size_t remaining_queries_;
        const Seconds query_timeout_sec_;
        const boost::optional<GetDns::TransportList>& transport_list_;
        const std::list<boost::asio::ip::address>& resolvers_;
        const std::list<GetDns::Data::TrustAnchor>& trust_anchors_;
        const struct ::timespec time_end_;
        GetDns::Extensions extensions_;
        typedef std::map< ::getdns_transaction_t, std::string > Tasks;
        Tasks tasks_;
    } timer(solver,
            _to_resolve,
            _query_timeout_sec,
            _transport_list,
            _resolvers,
            _trust_anchors,
            _assigned_time_nsec);
}

template <class T>
T split(const std::string& src, const std::string& delimiters, void(*append)(const std::string& item, T& container))
{
    if (src.empty())
    {
        return T();
    }
    std::vector<std::string> vector_of_items;
    boost::algorithm::split(vector_of_items, src, boost::algorithm::is_any_of(delimiters));
    T result;
    for (std::vector<std::string>::const_iterator item_itr = vector_of_items.begin();
            item_itr != vector_of_items.end(); ++item_itr)
    {
        append(*item_itr, result);
    }
    return result;
}

void append_ip_address(const std::string& item, std::list<boost::asio::ip::address>& addresses)
{
    addresses.push_back(boost::asio::ip::address::from_string(item));
}

void append_trust_anchor(const std::string& item, std::list<GetDns::Data::TrustAnchor>& anchors)
{
    std::istringstream anchor_stream(item);
    std::string zone;
    int flags;
    int protocol;
    int algorithm;
    std::string public_key;
    std::string base64_encoded_public_key;
    anchor_stream >> zone
                  >> flags
                  >> protocol
                  >> algorithm
                  >> base64_encoded_public_key;
    GetDns::Data::TrustAnchor trust_anchor;
    trust_anchor.zone = zone;
    trust_anchor.flags = flags;
    trust_anchor.protocol = protocol;
    trust_anchor.algorithm = algorithm;
    trust_anchor.public_key = GetDns::Data::base64_decode(base64_encoded_public_key);
    anchors.push_back(trust_anchor);
}

const char cmdline_help_text[] =
        "Scanner of CDNSKEY records.\n\n"
        "usage: cdnskey-scanner [--hostname_resolvers IP address[,...]] "
                               "[--cdnskey_resolvers IP address[,...]] "
                               "[--dnssec_trust_anchors anchor[,...]] "
                               "[--timeout sec] "
                               "RUNTIME | "
                               "--help\n\n"
        "    Arguments:\n"
        "        --hostname_resolvers ..... IP addresses of resolvers used for resolving A and AAAA\n"
        "                                   records of nameservers; default is in system configured\n"
        "                                   resolver\n"
        "        --cdnskey_resolvers ...... IP addresses of resolvers used for resolving signed CDNSKEY\n"
        "                                   records of domains; default is in system configured\n"
        "                                   resolver\n"
        "        --dnssec_trust_anchors ... chain of trust for verification of signed CDNSKEY records;\n"
        "                                   default is in system configured chain of trust\n"
        "            * anchor's format: zone flags protocol algorithm public_key_base64\n"
        "                       example: . 257 3 8 AwEAAdAjHYjq...xAU8=\n"
        "        --timeout ................ maximum time (in seconds) spent by one DNS request;\n"
        "                                   default is 10 seconds\n"
        "        RUNTIME .................. total time (in seconds) reserved for application run\n"
        "        --help ................... this help\n\n"
        "    Format of data received from standard input:\n"
        "        [secure]\n"
        "        podepsana1.cz podepsana2.cz ... podepsanaN.cz\n"
        "        [insecure]\n"
        "        nameserver1.cz domena1.cz domena2.cz ... domenaN.cz\n"
        "        nameserver2.sk blabla1.cz blabla2.cz ... blablaM.cz\n\n"
        "    Format of data sent to standard output:\n"
        "        insecure nameserver ip domain flags protocol algorithm public_key_base64\n"
        "        secure domain flags protocol algorithm public_key_base64\n"
        "        untrustworthy domain\n"
        "        unknown domain\n"
        "        unresolved nameserver ip domain\n";

}//namespace {anonymous}