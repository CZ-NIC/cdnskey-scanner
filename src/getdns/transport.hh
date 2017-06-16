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

#ifndef TRANSPORT_HH_7C85D5BE63ECB56E0176E62068801E57//date "+%s"|md5sum|tr "[a-f]" "[A-F]"
#define TRANSPORT_HH_7C85D5BE63ECB56E0176E62068801E57

#include <list>

namespace GetDns
{

struct Transport
{
    enum Protocol
    {
        udp,
        tcp,
        tls
    };
};

typedef std::list<Transport::Protocol> TransportList;

}//namespace GetDns

#endif//TRANSPORT_HH_7C85D5BE63ECB56E0176E62068801E57