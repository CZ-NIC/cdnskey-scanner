/*
 * Copyright (C) 2017-2018  CZ.NIC, z. s. p. o.
 *
 * This file is part of FRED.
 *
 * FRED is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FRED is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FRED.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef PIPE_HH_2749D0FE6C3EBE19B1146E002E14C660//date "+%s"|md5sum|tr "[a-f]" "[A-F]"
#define PIPE_HH_2749D0FE6C3EBE19B1146E002E14C660

namespace Util {

//public read end of pipe interface, hides write end of pipe
class ImReader;

//public write end of pipe interface, hides read end of pipe
class ImWriter;

class Pipe
{
public:
    Pipe();
    ~Pipe();
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
private:
    static const unsigned number_of_descriptors_ = 2;
    int fd_[number_of_descriptors_];

    friend class ImReader;
    friend class ImWriter;
};

class ImReader
{
public:
    ImReader(Pipe& _pipe);
    ~ImReader() { }
    ImReader(const ImReader&) = delete;
    ImReader& operator=(const ImReader&) = delete;
    void set_nonblocking()const;
    int get_descriptor()const;
private:
    Pipe& pipe_;
};

class ImWriter
{
public:
    enum class Stream
    {
        stdout,
        stderr
    };
    ImWriter(Pipe& _pipe, Stream _into);
    ~ImWriter() { }
    ImWriter(const ImWriter&) = delete;
    ImWriter& operator=(const ImWriter&) = delete;
private:
    Pipe& pipe_;
};

}//namespace Util

#endif//PIPE_HH_2749D0FE6C3EBE19B1146E002E14C660
