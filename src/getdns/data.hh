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

#ifndef DATA_HH_4BD03E3BE61C61ADC6A6590A94FC068D//date "+%s"|md5sum|tr "[a-f]" "[A-F]"
#define DATA_HH_4BD03E3BE61C61ADC6A6590A94FC068D

#include "src/getdns/solver_fwd.hh"

#include <getdns/getdns.h>

#include <string>
#include <set>
#include <iosfwd>

#include <boost/shared_ptr.hpp>
#include <boost/variant/variant.hpp>
#include <boost/asio/ip/address.hpp>

namespace GetDns {

template <class R, class S, class K> struct GetItem;
template <class D, class K, class V> struct SetItem;

struct Data
{
    class Dict;
    class List;
    struct Type
    {
        enum Enum
        {
            dictionary,
            array,
            binary,
            integer
        };
    };
    template <class T>
    struct HolderOf
    {
        explicit HolderOf(T* src, void(*destroy_fnc)(T*) = NULL)
            : ptr(src),
              destroy_routine(destroy_fnc) { }
        ~HolderOf() { if (destroy_routine != NULL) { destroy_routine(ptr); } }
        T* const ptr;
        void(*const destroy_routine)(T*);
    };
    typedef HolderOf< ::getdns_dict > HolderOfDictPtr;
    typedef HolderOf< ::getdns_list > HolderOfListPtr;
    struct Empty { };
    typedef boost::variant< boost::shared_ptr<HolderOfDictPtr>,
                            boost::shared_ptr<HolderOfListPtr>,
                            Empty > HolderOfDataPtr;
    struct LookUpResult
    {
        enum Enum
        {
            success,
            index_out_of_range,
            not_found,
            different_type
        };
    };
    class Dict
    {
    public:
        typedef ::getdns_dict Base;
        typedef boost::shared_ptr<HolderOfDictPtr> SharedBasePtr;
        Dict();
        Dict(const Dict& _src);
        ~Dict();
        Dict& operator=(const Dict& _src);
        Type::Enum get_data_type_of_item(const char* _key)const;
        Type::Enum get_data_type_of_item(const std::string& _key)const;
        typedef std::set<std::string> Keys;
        Keys get_keys()const;
        std::string get_pretty_string()const;
        Base* get_base_ptr();
        const Base* get_base_ptr()const;
        LookUpResult::Enum look_up(const char* _key, Type::Enum _type)const;
        friend std::ostream& operator<<(std::ostream& out, const Dict& data) { return out << data.get_pretty_string(); }
    private:
        explicit Dict(Base* _base);
        Dict(Base* _base, const boost::shared_ptr<HolderOfDictPtr>& _parent);
        Dict(Base* _base, const boost::shared_ptr<HolderOfListPtr>& _parent);
        SharedBasePtr base_ptr_;
        HolderOfDataPtr parent_;
        friend class Data;
        friend class List;
        friend class Solver;
        template <class R, class S, class K> friend struct GetItem;
        template <class D, class K, class V> friend struct SetItem;
    };
    class List
    {
    public:
        typedef ::getdns_list Base;
        typedef boost::shared_ptr<HolderOfListPtr> SharedBasePtr;
        List();
        List(const List& _src);
        ~List();
        List& operator=(const List& _src);
        std::size_t get_number_of_items()const;
        Type::Enum get_data_type_of_item(::size_t _index)const;
        LookUpResult::Enum look_up(::size_t _index, Type::Enum _type)const;
        const Base* get_base_ptr()const;
        Base* get_base_ptr();
    private:
        explicit List(Base* _base);
        List(Base* _base, const boost::shared_ptr<HolderOfDictPtr>& _parent);
        List(Base* _base, const boost::shared_ptr<HolderOfListPtr>& _parent);
        SharedBasePtr base_ptr_;
        HolderOfDataPtr parent_;
        friend class Data;
        friend class Dict;
        template <class R, class S, class K> friend struct GetItem;
        template <class D, class K, class V> friend struct SetItem;
    };
    template <class D, class K, class V>
    static D& set_item_of(D& _dst, K _index, V _value);
    struct Fqdn
    {
        std::string value;
    };
    struct NotSet { };
    struct NotRequestedType { };
    typedef boost::variant<
            Dict,
            List,
            ::uint32_t,
            std::string,
            Fqdn,
            boost::asio::ip::address,
            NotSet,
            NotRequestedType> Value;
    class Is
    {
    public:
        Is(const Value& _value):value_(_value) { }
        struct Type
        {
            explicit Type(bool value):type(value) { }
            const bool type;
        };
        template <typename T>
        Type of()const;
    private:
        const Value& value_;
    };
    class From
    {
    public:
        From(const Value& _value):value_(_value) { }
        template <typename T>
        const T& get_value_of()const;
    private:
        const Value& value_;
    };
    template <class R, class S, class K>
    static Value get(const S& _src, K _index);
};

}//namespace GetDns

#endif//DATA_HH_4BD03E3BE61C61ADC6A6590A94FC068D
